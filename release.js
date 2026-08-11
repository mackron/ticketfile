const childProcess = require("child_process");
const fs = require("fs");
const https = require("https");
const path = require("path");
const readline = require("readline");

const repositoryDirectory = __dirname;
const versionHeaderPath = path.join(repositoryDirectory, "source", "ticketfile_version.h");

function printUsage()
{
    console.log("USAGE:");
    console.log("    node release.js [--check | --yes]");
    console.log("");
    console.log("OPTIONS:");
    console.log("    --check");
    console.log("        Validate release state without creating or pushing a tag.");
    console.log("");
    console.log("    --yes");
    console.log("        Create and push the tag without a confirmation prompt.");
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

    if (arguments.length === 1 && (arguments[0] === "-h" || arguments[0] === "--help")) {
        printUsage();
        return;
    }

    if (arguments.length > 1 || (arguments.length === 1 && !checkOnly && !skipConfirmation && !printVersion)) {
        printUsage();
        throw new Error("Invalid command-line options.");
    }

    const version = readVersion();
    if (printVersion) {
        console.log(formatVersion(version));
        return;
    }

    const release = validateRelease(version);
    const tag = release.tag;

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

main().catch((error) => {
    console.error("ERROR: " + error.message);
    process.exitCode = 1;
});
