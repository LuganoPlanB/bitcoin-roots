import { validateTag } from "./release-catalogue.mjs";

const envelope = /^From ([a-f0-9]{40}|[a-f0-9]{64}) Mon Sep 17 00:00:00 2001$/;
const diffStart = /^diff --/;
const hunkStart = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/;

// Preserve the source spelling and line endings separately from display text.
function sourceLines(text) {
    return (text.match(/[^\n]*\n|[^\n]+$/g) ?? []).map((raw) => ({ raw, text: raw.replace(/\r?\n$/, "") }));
}

export function decodeMailHeader(value) {
    return value.replace(/(\?=)[ \t]+(?==\?)/g, "$1").replace(/=\?([^?]+)\?([BQ])\?([^?]*)\?=/gi,
        (original, charset, encoding, content) => {
            try {
                const bytes = encoding.toUpperCase() === "B" ? Buffer.from(content, "base64")
                    : Buffer.from(content.replace(/_/g, " ").replace(/=([a-f0-9]{2})/gi, (_, hex) => String.fromCharCode(parseInt(hex, 16))), "latin1");
                return new TextDecoder(charset, { fatal: true }).decode(bytes);
            } catch { return original; }
        });
}

// Git quotes paths as C strings, with octal escapes representing UTF-8 bytes.
export function decodeGitPath(value) {
    if (!value.startsWith('"')) return value;
    if (!value.endsWith('"')) throw new Error("Malformed quoted Git path");
    const bytes = [];
    const escapes = { a: 7, b: 8, t: 9, n: 10, v: 11, f: 12, r: 13, '"': 34, "\\": 92 };
    for (let i = 1; i < value.length - 1; i++) {
        if (value[i] !== "\\") {
            const point = value.codePointAt(i);
            bytes.push(...Buffer.from(String.fromCodePoint(point)));
            if (point > 0xffff) i++;
        } else {
            const octal = value.slice(i + 1).match(/^[0-7]{1,3}/)?.[0];
            if (octal) {
                const byte = parseInt(octal, 8);
                if (byte > 255) throw new Error("Invalid Git path byte escape");
                bytes.push(byte);
                i += octal.length;
            }
            else if (Object.hasOwn(escapes, value[i + 1])) bytes.push(escapes[value[++i]]);
            else throw new Error("Unsupported Git path escape");
        }
    }
    return new TextDecoder("utf-8", { fatal: true }).decode(Uint8Array.from(bytes));
}

function headerPaths(line) {
    const rest = line.slice("diff --git ".length);
    const quoted = rest.match(/^("(?:[^"\\]|\\.)*") ("(?:[^"\\]|\\.)*")$/);
    if (quoted) return quoted.slice(1).map(decodeGitPath);
    // Unquoted spaces are legal. Equal old/new paths are unambiguous even
    // when the path itself contains the delimiter; changed paths use metadata.
    for (let i = 0; i < rest.length; i++) {
        if (rest.slice(i, i + 3) === " b/" && rest.slice(2, i) === rest.slice(i + 3)) return [rest.slice(0, i), rest.slice(i + 1)];
    }
    const simple = rest.match(/^(a\/\S+) (b\/\S+)$/);
    return simple ? simple.slice(1) : [null, null];
}

function fileEntry(lines, tag, commitOrdinal, ordinal) {
    const raw = lines.map((line) => line.raw).join("");
    const texts = lines.map((line) => line.text);
    let [oldPath, newPath] = texts[0].startsWith("diff --git ") ? headerPaths(texts[0]) : [null, null];
    let inHunks = false;
    const metadata = [];
    for (const line of texts.slice(1)) {
        if (line.startsWith("@@")) inHunks = true;
        if (inHunks) continue;
        if (/^(old mode|new mode|new file mode|deleted file mode|similarity index|dissimilarity index|rename from|rename to|copy from|copy to|index) /.test(line)) metadata.push(line);
        if (line.startsWith("--- ")) oldPath = decodeGitPath(line.slice(4).split("\t")[0]);
        if (line.startsWith("+++ ")) newPath = decodeGitPath(line.slice(4).split("\t")[0]);
        if (/^(rename|copy) from /.test(line)) oldPath = `a/${decodeGitPath(line.replace(/^(rename|copy) from /, ""))}`;
        if (/^(rename|copy) to /.test(line)) newPath = `b/${decodeGitPath(line.replace(/^(rename|copy) to /, ""))}`;
    }
    const supported = texts[0].startsWith("diff --git ") && oldPath !== null && newPath !== null
        && !texts.some((line) => line.startsWith("@@") && !hunkStart.test(line));
    const binary = texts.some((line) => line === "GIT binary patch" || /^Binary files .* differ$/.test(line));
    const hasHunks = texts.some((line) => hunkStart.test(line));
    const state = !supported ? "unsupported" : binary ? "binary" : hasHunks ? "text" : "metadata-only";
    const strip = (path) => path === "/dev/null" ? null : path?.replace(/^[ab]\//, "") ?? null;
    oldPath = strip(oldPath);
    newPath = strip(newPath);
    return { id: `${tag}-c${commitOrdinal}-f${ordinal}`, ordinal, oldPath, newPath,
        path: newPath ?? oldPath, state, metadata, raw,
        reason: state === "unsupported" ? "Unsupported diff format or ambiguous paths; consult the original patch." : null };
}

function readHeaders(lines, start) {
    const headers = {};
    let previous;
    let end = start;
    for (; end < lines.length && lines[end].text !== ""; end++) {
        const line = lines[end].text;
        if (/^[ \t]/.test(line) && previous) headers[previous] += ` ${line.trim()}`;
        else {
            const match = line.match(/^([A-Za-z-]+):[ \t]*(.*)$/);
            if (!match) throw new Error("Malformed format-patch mail headers");
            previous = match[1].toLowerCase();
            if (Object.hasOwn(headers, previous)) throw new Error("Duplicate format-patch mail header");
            headers[previous] = match[2];
        }
    }
    if (!headers.subject || end === lines.length) throw new Error("Missing format-patch subject or header boundary");
    return { headers, end };
}

export function parsePatchSeries(input, tag) {
    validateTag(tag);
    const text = typeof input === "string" ? input : new TextDecoder("utf-8", { fatal: true }).decode(input);
    const lines = sourceLines(text);
    if (!lines.length || !envelope.test(lines[0].text)) throw new Error("Expected a Git format-patch mail series");
    const commits = [];
    let commit;
    let fileLines;
    let remainingOld = 0;
    let remainingNew = 0;
    let patchBody = false;
    const flushFile = () => {
        if (fileLines) commit.files.push(fileEntry(fileLines, tag, commit.ordinal, commit.files.length + 1));
        fileLines = null;
    };
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (remainingOld || remainingNew) {
            const prefix = line.text[0];
            if (prefix === " " || prefix === "-") remainingOld--;
            if (prefix === " " || prefix === "+") remainingNew--;
            if (![" ", "-", "+", "\\"].includes(prefix) || remainingOld < 0 || remainingNew < 0) throw new Error("Malformed diff hunk; refusing incomplete change coverage");
            fileLines.push(line);
            continue;
        }
        const mail = line.text.match(envelope);
        if (mail) {
            flushFile();
            const { headers, end } = readHeaders(lines, i + 1);
            const ordinal = commits.length + 1;
            commit = { id: `${tag}-c${ordinal}`, ordinal, originalCommit: mail[1],
                subject: decodeMailHeader(headers.subject), author: headers.from ? decodeMailHeader(headers.from) : null,
                date: headers.date ?? null, headers, files: [] };
            commits.push(commit);
            patchBody = false;
            i = end;
        } else if (!fileLines && line.text === "---") {
            patchBody = true;
        } else if (patchBody && diffStart.test(line.text)) {
            flushFile();
            fileLines = [line];
        } else if (fileLines) {
            if (line.text === "-- ") { flushFile(); continue; }
            const hunk = line.text.match(hunkStart);
            if (hunk) {
                remainingOld = Number(hunk[2] ?? 1);
                remainingNew = Number(hunk[4] ?? 1);
                if (!Number.isSafeInteger(remainingOld) || !Number.isSafeInteger(remainingNew)) throw new Error("Invalid diff hunk line count");
            }
            fileLines.push(line);
        }
    }
    if (remainingOld || remainingNew) throw new Error("Truncated diff hunk; refusing incomplete change coverage");
    flushFile();
    for (const item of commits) item.state = item.files.length ? "changes" : "no-diff";
    return { tag, commits, fileCount: commits.reduce((sum, item) => sum + item.files.length, 0) };
}
