const childProcess = require("child_process");
const fs = require("fs");
const https = require("https");
const path = require("path");
const readline = require("readline");

const repositoryDirectory = __dirname;
const versionHeaderPath = path.join(repositoryDirectory, "source", "ticketfile_version.h");
const ticketsDirectory = path.join(repositoryDirectory, "tickets");

function printUsage()
{
    console.log("USAGE:");
    console.log("    node release.js [--check | --yes | --write-notes <path> | --test]");
    console.log("");
    console.log("OPTIONS:");
    console.log("    --check");
    console.log("        Validate release state without creating or pushing a tag.");
    console.log("");
    console.log("    --yes");
    console.log("        Create and push the tag without a confirmation prompt.");
    console.log("");
    console.log("    --write-notes <path>");
    console.log("        Write curated release notes for the current version.");
    console.log("");
    console.log("    --test");
    console.log("        Run release validation tests.");
    console.log("");
    console.log("    -h, --help");
    console.log("        Show this help text.");
}

function readVersionPart(source, name)
{
    const expression = new RegExp("^\\s*#define\\s+" + name + "\\s+(\\d+)\\s*$", "m");
    const match = expression.exec(source);

    if (match === null) {
        throw new Error("Cannot find " + name + " in " + versionHeaderPath + ".");
    }

    return Number(match[1]);
}

function readVersion()
{
    const source = fs.readFileSync(versionHeaderPath, "utf8");

    return [
        readVersionPart(source, "TICKETFILE_VERSION_MAJOR"),
        readVersionPart(source, "TICKETFILE_VERSION_MINOR"),
        readVersionPart(source, "TICKETFILE_VERSION_PATCH")
    ];
}

function formatVersion(version)
{
    return version.join(".");
}

function compareVersions(versionA, versionB)
{
    for (let i = 0; i < 3; i += 1) {
        if (versionA[i] !== versionB[i]) {
            return versionA[i] < versionB[i] ? -1 : 1;
        }
    }

    return 0;
}

function parseVersionTag(tag)
{
    const match = /^v(\d+)\.(\d+)\.(\d+)$/.exec(tag);

    if (match === null) {
        return null;
    }

    return [Number(match[1]), Number(match[2]), Number(match[3])];
}

function findTicketSeparator(text, start)
{
    let cursor = start;

    while (cursor < text.length) {
        const lineStart = cursor;

        while (cursor < text.length && text[cursor] !== "\n") {
            cursor += 1;
        }

        const lineEnd = cursor;
        if (text.substring(lineStart, lineEnd).trim() === "---") {
            return { lineStart, nextLine: cursor < text.length ? cursor + 1 : cursor };
        }

        cursor += 1;
    }

    return undefined;
}

function readTicketMetadata(text, end)
{
    const metadata = new Map();

    for (const line of text.substring(0, end).split(/\r?\n/)) {
        const separator = line.indexOf(":");

        if (separator >= 0) {
            metadata.set(line.substring(0, separator).trim(), line.substring(separator + 1).trim());
        }
    }

    return metadata;
}

function extractReleaseBody(text)
{
    const metadataSeparator = findTicketSeparator(text, 0);
    if (metadataSeparator === undefined) {
        throw new Error("Release-note ticket has no metadata separator.");
    }

    const commentSeparator = findTicketSeparator(text, metadataSeparator.nextLine);
    const descriptionEnd = commentSeparator === undefined ? text.length : commentSeparator.lineStart;
    const description = text.substring(metadataSeparator.nextLine, descriptionEnd);
    const shortDescription = /\S.*(?:\r?\n|$)/.exec(description);

    if (shortDescription === null) {
        throw new Error("Release-note ticket has no short description.");
    }

    const bodyStart = shortDescription.index + shortDescription[0].length;
    const body = description.substring(bodyStart).replace(/^(?:[ \t]*\r?\n)+/, "").replace(/\s+$/, "");

    if (body === "") {
        throw new Error("Release-note ticket has an empty release body.");
    }

    return body + "\n";
}

function getReleaseNotes(versionText, directory = ticketsDirectory)
{
    const matches = [];

    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        if (!entry.isFile() || !/^\d+$/.test(entry.name)) {
            continue;
        }

        const ticketPath = path.join(directory, entry.name);
        const text = fs.readFileSync(ticketPath, "utf8");
        const metadataSeparator = findTicketSeparator(text, 0);

        if (metadataSeparator !== undefined &&
            readTicketMetadata(text, metadataSeparator.lineStart).get("release-notes") === versionText) {
            matches.push(text);
        }
    }

    if (matches.length === 0) {
        throw new Error("No release-note ticket exists for version " + versionText + ".");
    }
    if (matches.length > 1) {
        throw new Error("Multiple release-note tickets exist for version " + versionText + ".");
    }

    return extractReleaseBody(matches[0]);
}

function getIncompleteReleaseTickets(versionText, directory = ticketsDirectory)
{
    const matches = [];

    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        if (!entry.isFile() || !/^\d+$/.test(entry.name)) {
            continue;
        }

        const ticketPath = path.join(directory, entry.name);
        const text = fs.readFileSync(ticketPath, "utf8");
        const metadataSeparator = findTicketSeparator(text, 0);

        if (metadataSeparator === undefined) {
            continue;
        }

        const metadata = readTicketMetadata(text, metadataSeparator.lineStart);
        if (metadata.get("version") === versionText && metadata.get("status") !== "closed") {
            matches.push({ number: entry.name, status: metadata.get("status") || "no status" });
        }
    }

    matches.sort((ticketA, ticketB) => Number(ticketA.number) - Number(ticketB.number));
    return matches;
}

function validateReleaseTickets(versionText, directory = ticketsDirectory)
{
    const matches = getIncompleteReleaseTickets(versionText, directory);

    if (matches.length !== 0) {
        const details = matches.map((ticket) =>
            "    " + ticket.number + " (" + ticket.status + ")").join("\n");
        throw new Error("Release " + versionText + " has non-closed tickets:\n" + details);
    }
}

function writeReleaseNotes(versionText, outputPath)
{
    validateReleaseTickets(versionText);
    const notes = getReleaseNotes(versionText);

    fs.mkdirSync(path.dirname(path.resolve(outputPath)), { recursive: true });
    fs.writeFileSync(outputPath, notes, "utf8");
}

function runReleaseTests()
{
    const assert = require("assert");
    const os = require("os");
    const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "ticketfile-release-test-"));

    function writeTicket(name, text)
    {
        fs.writeFileSync(path.join(temporaryDirectory, name), text, "utf8");
    }

    try {
        const validTicket = "status: open\nrelease-notes: 1.1.0\n\n---\n\n" +
            "Release Notes - v1.1.0\n\n## CLI\n\n- Added metadata commands.\n\n---\n\nComment.\n";

        assert.strictEqual(extractReleaseBody(validTicket), "## CLI\n\n- Added metadata commands.\n");

        writeTicket("1", validTicket);
        writeTicket("not-a-ticket", validTicket.replace("1.1.0", "ignored"));
        fs.mkdirSync(path.join(temporaryDirectory, "3"));
        assert.strictEqual(getReleaseNotes("1.1.0", temporaryDirectory),
            "## CLI\n\n- Added metadata commands.\n");
        assert.throws(() => getReleaseNotes("1.1", temporaryDirectory), /No release-note ticket/);

        writeTicket("2", validTicket.replace("Release Notes - v1.1.0", "Other release notes"));
        assert.throws(() => getReleaseNotes("1.1.0", temporaryDirectory), /Multiple release-note tickets/);

        assert.throws(() => extractReleaseBody("release-notes: 1.1.0\nNo separator.\n"),
            /no metadata separator/);
        assert.throws(() => extractReleaseBody("release-notes: 1.1.0\n\n---\n\nShort description.\n"),
            /empty release body/);

        writeTicket("10", "status: closed\nversion: 1.2.0\n\n---\n\nClosed.\n");
        writeTicket("4", "status: review\nversion: 1.2.0\n\n---\n\nReview.\n");
        writeTicket("12", "version: 1.2.0\n\n---\n\nNo status.\n");
        writeTicket("5", "status: open\nversion: 1.3.0\n\n---\n\nOther version.\n");
        assert.deepStrictEqual(getIncompleteReleaseTickets("1.2.0", temporaryDirectory), [
            { number: "4", status: "review" },
            { number: "12", status: "no status" }
        ]);
        assert.throws(() => validateReleaseTickets("1.2.0", temporaryDirectory),
            /Release 1\.2\.0 has non-closed tickets:\n    4 \(review\)\n    12 \(no status\)/);
        assert.doesNotThrow(() => validateReleaseTickets("1.3.1", temporaryDirectory));
    } finally {
        fs.rmSync(temporaryDirectory, { recursive: true, force: true });
    }

    console.log("PASS: release checks");
}

function runGit(arguments, allowFailure)
{
    const result = childProcess.spawnSync("git", arguments, {
        cwd: repositoryDirectory,
        encoding: "utf8"
    });

    if (result.error) {
        throw result.error;
    }

    if (result.status !== 0 && !allowFailure) {
        const details = result.stderr.trim() || result.stdout.trim();
        throw new Error("Git command failed: git " + arguments.join(" ") + (details ? "\n" + details : ""));
    }

    return result;
}

function getGitHubRepository()
{
    const remote = runGit(["remote", "get-url", "origin"], false).stdout.trim();
    const match = /^(?:https:\/\/github\.com\/|git@github\.com:|ssh:\/\/git@github\.com\/)([^/]+)\/([^/]+?)(?:\.git)?$/i.exec(remote);

    if (match === null) {
        throw new Error("Origin is not a supported GitHub repository URL: " + remote);
    }

    return match[1] + "/" + match[2];
}

function requestGitHub(pathName)
{
    return new Promise((resolve, reject) => {
        const headers = {
            "Accept": "application/vnd.github+json",
            "User-Agent": "ticketfile-release",
            "X-GitHub-Api-Version": "2022-11-28"
        };

        if (process.env.GITHUB_TOKEN) {
            headers.Authorization = "Bearer " + process.env.GITHUB_TOKEN;
        }

        const request = https.get({
            hostname: "api.github.com",
            path: pathName,
            headers
        }, (response) => {
            let responseText = "";

            response.setEncoding("utf8");
            response.on("data", (chunk) => {
                responseText += chunk;
            });
            response.on("end", () => {
                let responseData;

                try {
                    responseData = JSON.parse(responseText);
                } catch (error) {
                    reject(new Error("GitHub returned an invalid response."));
                    return;
                }

                if (response.statusCode < 200 || response.statusCode >= 300) {
                    reject(new Error("GitHub API request failed: " +
                        (responseData.message || "HTTP " + response.statusCode) + "."));
                    return;
                }

                resolve(responseData);
            });
        });

        request.on("error", reject);
    });
}

async function validateContinuousIntegration(head)
{
    const repository = getGitHubRepository();
    const requestPath = "/repos/" + repository + "/actions/workflows/build.yml/runs?head_sha=" + encodeURIComponent(head) + "&event=push&per_page=10";
    const response = await requestGitHub(requestPath);
    const runs = response.workflow_runs;

    if (!Array.isArray(runs) || runs.length === 0) {
        throw new Error("No Build workflow run exists for commit " + head + ".");
    }

    const run = runs[0];
    if (run.status !== "completed") {
        throw new Error("Build workflow is not complete for commit " + head + ". Status: " + run.status + ".\n" + run.html_url);
    }

    if (run.conclusion !== "success") {
        throw new Error("Build workflow did not succeed for commit " + head + ". Conclusion: " + run.conclusion + ".\n" + run.html_url);
    }
}

function validateRelease(version)
{
    const versionText = formatVersion(version);
    const tag = "v" + versionText;
    const status = runGit(["status", "--porcelain"], false).stdout.trim();
    const branch = runGit(["branch", "--show-current"], false).stdout.trim();
    let newestVersion = null;
    let newestTag = null;

    if (status !== "") {
        throw new Error("Working tree is not clean. Commit or discard all changes first.");
    }

    if (branch !== "master") {
        throw new Error("Releases must be created from master. Current branch: " + (branch || "detached HEAD") + ".");
    }

    runGit(["fetch", "--quiet", "origin", "+refs/heads/master:refs/remotes/origin/master", "--tags"], false);

    const head       = runGit(["rev-parse", "HEAD"],          false).stdout.trim();
    const remoteHead = runGit(["rev-parse", "origin/master"], false).stdout.trim();
    if (head !== remoteHead) {
        throw new Error("Current commit does not match origin/master. Push or update master first.");
    }

    const tags = runGit(["tag", "--list", "v*"], false).stdout.split(/\r?\n/);
    for (const existingTag of tags) {
        const existingVersion = parseVersionTag(existingTag);

        if (existingVersion !== null && (newestVersion === null || compareVersions(existingVersion, newestVersion) > 0)) {
            newestVersion = existingVersion;
            newestTag = existingTag;
        }
    }

    if (newestVersion !== null && compareVersions(version, newestVersion) <= 0) {
        throw new Error(
            "Version " + versionText + " has not been increased. " +
            "Newest release tag is " + newestTag + ". Update " + versionHeaderPath + "."
        );
    }

    if (runGit(["rev-parse", "--verify", "--quiet", "refs/tags/" + tag], true).status === 0) {
        throw new Error("Tag " + tag + " already exists. Update " + versionHeaderPath + ".");
    }

    return { tag, head };
}

function confirmRelease(tag)
{
    const input = readline.createInterface({ input: process.stdin, output: process.stdout });

    return new Promise((resolve) => {
        input.question("Create and push " + tag + "? [y/N] ", (answer) => {
            input.close();
            resolve(answer.toLowerCase() === "y" || answer.toLowerCase() === "yes");
        });
    });
}

async function main()
{
    const arguments = process.argv.slice(2);
    const checkOnly = arguments.length === 1 && arguments[0] === "--check";
    const skipConfirmation = arguments.length === 1 && arguments[0] === "--yes";
    const printVersion = arguments.length === 1 && arguments[0] === "--print-version";
    const writeNotes = arguments.length === 2 && arguments[0] === "--write-notes";
    const runTests = arguments.length === 1 && arguments[0] === "--test";

    if (arguments.length === 1 && (arguments[0] === "-h" || arguments[0] === "--help")) {
        printUsage();
        return;
    }

    if ((!writeNotes && arguments.length > 1) ||
        (arguments.length === 1 && !checkOnly && !skipConfirmation && !printVersion && !runTests)) {
        printUsage();
        throw new Error("Invalid command-line options.");
    }

    const version = readVersion();
    const versionText = formatVersion(version);
    if (runTests) {
        runReleaseTests();
        return;
    }
    if (printVersion) {
        console.log(versionText);
        return;
    }
    if (writeNotes) {
        writeReleaseNotes(versionText, arguments[1]);
        return;
    }

    getReleaseNotes(versionText);

    const release = validateRelease(version);
    const tag = release.tag;

    validateReleaseTickets(versionText);

    await validateContinuousIntegration(release.head);

    console.log("Release state is valid for " + tag + ".");
    if (checkOnly) {
        return;
    }

    if (!skipConfirmation && !await confirmRelease(tag)) {
        console.log("Release cancelled.");
        return;
    }

    runGit(["tag", "--annotate", tag, "--message", "Release " + tag + "."], false);

    const pushResult = runGit(["push", "origin", tag], true);
    if (pushResult.status !== 0) {
        const details = pushResult.stderr.trim() || pushResult.stdout.trim();
        throw new Error("Tag " + tag + " was created locally, but push failed." +
            (details ? "\n" + details : ""));
    }

    console.log("Pushed " + tag + ". GitHub release automation can now build release files.");
}

if (require.main === module) {
    main().catch((error) => {
        console.error("ERROR: " + error.message);
        process.exitCode = 1;
    });
}
