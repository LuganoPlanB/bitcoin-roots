import assert from "node:assert/strict";
import test from "node:test";
import { decodeGitPath, decodeMailHeader, parsePatchSeries } from "../../scripts/patch-series.mjs";

const tag = "v29.4-roots.4";
const mail = (diff, { id = "a".repeat(40), subject = "[PATCH] A change", message = "A description.", author = "Author <author@example.test>" } = {}) =>
    `From ${id} Mon Sep 17 00:00:00 2001\nFrom: ${author}\nDate: Thu, 1 Oct 2026 12:00:00 +0000\nSubject: ${subject}\n\n${message}\n---\n file | 1 +\n\n${diff}-- \n2.43.0\n\n`;
const textDiff = (path = "file", text = "added") =>
    `diff --git a/${path} b/${path}\nindex 1234567..abcdef0 100644\n--- a/${path}\n+++ b/${path}\n@@ -1 +1 @@\n-old\n+${text}\n`;

test("ordered mails preserve original IDs, decoded folded metadata and repeated paths", () => {
    const input = mail(textDiff(), { subject: "[PATCH 1/2] =?UTF-8?Q?caf=C3=A9?=\n continued", author: "=?UTF-8?B?Sm9zw6k=?= <author@example.test>" })
        + mail(textDiff(), { id: "b".repeat(40), subject: "[PATCH 2/2] Again" });
    const result = parsePatchSeries(Buffer.from(input), tag);
    assert.equal(result.commits.length, 2);
    assert.equal(result.fileCount, 2);
    assert.equal(result.commits[0].subject, "[PATCH 1/2] café continued");
    assert.equal(result.commits[0].author, "José <author@example.test>");
    assert.equal(result.commits[1].originalCommit, "b".repeat(40));
    assert.equal(result.commits[0].files[0].path, result.commits[1].files[0].path);
    assert.notEqual(result.commits[0].files[0].id, result.commits[1].files[0].id);
    assert.deepEqual(parsePatchSeries(input, tag), result);
    assert.equal(result.commits[0].files[0].raw, textDiff());
});

test("mail, diff and signature-like text inside counted hunks remains source text", () => {
    const diff = `diff --git a/file b/file\n--- a/file\n+++ b/file\n@@ -1,7 +1,7 @@\n From ${"c".repeat(40)} Mon Sep 17 00:00:00 2001\n From: Fake <fake@example.test>\n Subject: Fake commit\n \n diff --git a/fake b/fake\n--- \n+-- \n-last\n+replacement\n`;
    const result = parsePatchSeries(mail(diff), tag);
    assert.equal(result.commits.length, 1);
    assert.equal(result.fileCount, 1);
    assert.equal(result.commits[0].files[0].raw, diff);
});

test("diff-like commit prose before the patch separator does not invent files", () => {
    const result = parsePatchSeries(mail(textDiff(), { message: "Example:\ndiff --git a/example b/example\nFrom: example" }), tag);
    assert.equal(result.fileCount, 1);
    assert.equal(result.commits[0].files[0].path, "file");
});

test("CRLF and no-newline markers survive without changing hunk counts", () => {
    const diff = textDiff("file", "</script><script>alert(1)</script>") + "\\ No newline at end of file\n";
    const result = parsePatchSeries(Buffer.from(mail(diff).replaceAll("\n", "\r\n")), tag);
    assert.equal(result.commits[0].files[0].raw, diff.replaceAll("\n", "\r\n"));
    assert.equal(result.commits[0].files[0].state, "text");
});

test("every addition, deletion, symlink, rename, copy, mode, empty and binary section has an entry", () => {
    const sections = [
        "diff --git a/added b/added\nnew file mode 100644\n--- /dev/null\n+++ b/added\n@@ -0,0 +1 @@\n+new\n",
        "diff --git a/deleted b/deleted\ndeleted file mode 100644\n--- a/deleted\n+++ /dev/null\n@@ -1 +0,0 @@\n-old\n",
        "diff --git a/link b/link\nnew file mode 120000\n--- /dev/null\n+++ b/link\n@@ -0,0 +1 @@\n+target\n\\ No newline at end of file\n",
        "diff --git a/old name b/new name\nsimilarity index 100%\nrename from old name\nrename to new name\n",
        "diff --git a/old b/copy\nsimilarity index 100%\ncopy from old\ncopy to copy\n",
        "diff --git a/mode b/mode\nold mode 100644\nnew mode 100755\n",
        "diff --git a/empty b/empty\nnew file mode 100644\nindex 0000000..e69de29\n",
        "diff --git a/image b/image\nindex 1234567..abcdef0 100644\nGIT binary patch\nliteral 1\nIc${Nk000310RR91\n",
        "diff --git a/image2 b/image2\nBinary files a/image2 and b/image2 differ\n",
    ];
    const files = parsePatchSeries(mail(sections.join("")), tag).commits[0].files;
    assert.equal(files.length, sections.length);
    assert.deepEqual(files.map((f) => f.state), ["text", "text", "text", "metadata-only", "metadata-only", "metadata-only", "metadata-only", "binary", "binary"]);
    assert.equal(files[0].oldPath, null);
    assert.equal(files[1].newPath, null);
    assert.equal(files[3].oldPath, "old name");
    assert.equal(files[3].newPath, "new name");
    assert.ok(files[2].metadata.includes("new file mode 120000"));
    assert.deepEqual(files.map((f) => f.raw), sections);
    assert.equal(new Set(files.map((f) => f.id)).size, sections.length);
});

test("quoted Unicode, escaped paths and spaces are display data, never route identity", () => {
    const diff = 'diff --git "a/caf\\303\\251 \\"x\\".txt" "b/caf\\303\\251 \\"x\\".txt"\nold mode 100644\nnew mode 100755\n';
    const file = parsePatchSeries(mail(diff), tag).commits[0].files[0];
    assert.equal(file.path, 'café "x".txt');
    assert.equal(file.id, `${tag}-c1-f1`);
    assert.equal(parsePatchSeries(mail(textDiff("a b/with spaces")), tag).commits[0].files[0].path, "a b/with spaces");
    const hostile = parsePatchSeries(mail(textDiff("../<img onerror=alert(1)>")), tag).commits[0].files[0];
    assert.equal(hostile.path, "../<img onerror=alert(1)>");
    assert.equal(hostile.id, file.id);
    assert.equal(decodeGitPath('"a/with\\tTAB\\nnewline"'), "a/with\tTAB\nnewline");
});

test("binary-only commits and unsupported diff formats remain explicit", () => {
    const result = parsePatchSeries(mail("diff --git a/b b/b\nGIT binary patch\nliteral 0\nHcmV?d00001\n")
        + mail("diff --cc merged\nindex abc,def..123\n@@@ -1,1 -1,1 +1,1 @@@\n++merged\n", { id: "b".repeat(40) }), tag);
    assert.equal(result.commits.length, 2);
    assert.equal(result.commits[0].files[0].state, "binary");
    assert.equal(result.commits[1].files[0].state, "unsupported");
    assert.match(result.commits[1].files[0].reason, /original patch/);
    const ambiguous = parsePatchSeries(mail("diff --git a/ambiguous old b/ambiguous new\nold mode 100644\nnew mode 100755\n"), tag);
    assert.equal(ambiguous.commits[0].files[0].state, "unsupported");
    assert.equal(parsePatchSeries(mail("diff --git a/a b/a\n@@ invalid hunk\n+unknown\n"), tag).commits[0].files[0].state, "unsupported");
    assert.equal(parsePatchSeries(mail(""), tag).commits[0].state, "no-diff");
});

test("malformed and truncated inputs fail explicitly rather than return partial clean coverage", () => {
    assert.throws(() => parsePatchSeries("not a patch", tag), /format-patch/);
    assert.throws(() => parsePatchSeries(mail(textDiff()), "../../bad"), /tag/);
    assert.throws(() => parsePatchSeries(Buffer.from([255]), tag), /encoded data/);
    assert.throws(() => parsePatchSeries(mail(textDiff().replace("@@ -1 +1 @@", "@@ -1,10 +1,10 @@")), tag), /hunk/);
    assert.throws(() => parsePatchSeries(mail(textDiff().replace("-old\n", "bad line\n")), tag), /hunk/);
    assert.throws(() => parsePatchSeries(`From ${"a".repeat(40)} Mon Sep 17 00:00:00 2001\nSubject: first\nSubject: second\n\n`, tag), /Duplicate/);
    assert.throws(() => decodeGitPath('"a/\\q"'), /escape/);
    assert.throws(() => decodeGitPath('"a/\\777"'), /byte escape/);
    assert.equal(decodeMailHeader("=?unknown-charset?B?YQ==?="), "=?unknown-charset?B?YQ==?=");
});
