import { html } from "diff2html";

export const rendererSettings = Object.freeze({ matching: "none", drawFileList: false,
    maxLineLengthHighlight: 0, colorScheme: "light" });
export const renderLimits = Object.freeze({ linesPerPage: 400, lineCharacters: 4096,
    diffHtmlBytes: 1800000, documentBytes: 2000000, overviewBytes: 500000 });

// Feed the renderer counted JSON rows rather than its permissive string parser,
// which normalizes CRLF and removes no-newline markers throughout the input.
export function toDiffFile(file) {
    const result = { oldName: file.oldPath ?? "/dev/null", newName: file.newPath ?? "/dev/null",
        isGitDiff: true, isCombined: false, language: "text", addedLines: 0, deletedLines: 0,
        isNew: file.oldPath === null, isDeleted: file.newPath === null, blocks: [] };
    let block;
    let oldNumber;
    let newNumber;
    for (const line of file.raw.split("\n").map((line) => line.replace(/\r$/, ""))) {
        const match = line.match(/^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@/);
        if (match) {
            oldNumber = Number(match[1]);
            newNumber = Number(match[2]);
            block = { header: line, oldStartLine: oldNumber, newStartLine: newNumber, lines: [] };
            result.blocks.push(block);
        } else if (block && /^[ +\-\\]/.test(line)) {
            const type = line[0] === "+" ? "insert" : line[0] === "-" ? "delete" : "context";
            const marker = line.startsWith("\\ No newline at end of file");
            block.lines.push({ content: line, type, oldNumber: marker || type === "insert" ? undefined : oldNumber++,
                newNumber: marker || type === "delete" ? undefined : newNumber++ });
            if (type === "insert") result.addedLines++;
            if (type === "delete") result.deletedLines++;
        }
    }
    return result;
}

function selectRows(file, start, end) {
    let offset = 0;
    const blocks = [];
    for (const block of file.blocks) {
        const selected = block.lines.slice(Math.max(0, start - offset), Math.max(0, end - offset));
        if (selected.length) blocks.push({ ...block, lines: selected,
            header: start > offset ? `${block.header} (continued)` : block.header });
        offset += block.lines.length;
    }
    return { ...file, blocks,
        addedLines: blocks.flatMap((block) => block.lines).filter((line) => line.type === "insert").length,
        deletedLines: blocks.flatMap((block) => block.lines).filter((line) => line.type === "delete").length };
}

export function renderFilePages(file, limits = renderLimits) {
    if (![limits.linesPerPage, limits.lineCharacters, limits.diffHtmlBytes].every((value) => Number.isSafeInteger(value) && value > 0)) throw new Error("Invalid diff rendering limits");
    if (file.state !== "text") return { state: file.state, reason: file.reason, pages: [] };
    const data = toDiffFile(file);
    const rows = data.blocks.flatMap((block) => block.lines);
    if (!rows.length) return { state: "unsupported", reason: "No supported text hunks; consult the original patch.", pages: [] };
    if (rows.some((line) => line.content.length > limits.lineCharacters)
        || data.blocks.some((block) => block.header.length > limits.lineCharacters)) {
        return { state: "oversized", reason: `A source line exceeds the ${limits.lineCharacters}-character display limit. The complete change is in the original patch.`, pages: [] };
    }
    const pages = [];
    const render = (start, end) => {
        const selected = selectRows(data, start, end);
        const unified = html([selected], { ...rendererSettings, outputFormat: "line-by-line" });
        const sideBySide = html([selected], { ...rendererSettings, outputFormat: "side-by-side" });
        if (Math.max(Buffer.byteLength(unified), Buffer.byteLength(sideBySide)) > limits.diffHtmlBytes) {
            if (end - start === 1) throw new Error("A single diff row exceeds the HTML budget");
            const middle = start + Math.floor((end - start) / 2);
            render(start, middle);
            render(middle, end);
        } else pages.push({ unified, sideBySide, start, end });
    };
    for (let start = 0; start < rows.length; start += limits.linesPerPage) render(start, Math.min(rows.length, start + limits.linesPerPage));
    return { state: "text", pages, lineCount: rows.length };
}
