const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const extensionDirectory = __dirname;
const repositoryDirectory = path.resolve(extensionDirectory, "../..");
const versionHeaderPath = path.join(repositoryDirectory, "source", "ticketfile_version.h");
const packagePath = path.join(extensionDirectory, "package.json");

function readVersionPart(source, name) {
    const expression = new RegExp("^\\s*#define\\s+" + name + "\\s+(\\d+)\\s*$", "m");
    const match = expression.exec(source);

    if (match === null) {
        throw new Error("Cannot find " + name + " in " + versionHeaderPath + ".");
    }

    return match[1];
}

function copyDirectory(sourcePath, destinationPath) {
    const entries = fs.readdirSync(sourcePath, { withFileTypes: true });

    fs.mkdirSync(destinationPath, { recursive: true });

    for (const entry of entries) {
        const sourceEntryPath = path.join(sourcePath, entry.name);
        const destinationEntryPath = path.join(destinationPath, entry.name);

        if (entry.isDirectory()) {
            copyDirectory(sourceEntryPath, destinationEntryPath);
        } else if (entry.isFile()) {
            fs.copyFileSync(sourceEntryPath, destinationEntryPath);
        }
    }
}

function writePackage(packageData, destinationPath) {
    fs.writeFileSync(destinationPath, JSON.stringify(packageData, null, 4) + "\n");
}

function main() {
    const versionSource = fs.readFileSync(versionHeaderPath, "utf8");
    const version = [
        readVersionPart(versionSource, "TICKETFILE_VERSION_MAJOR"),
        readVersionPart(versionSource, "TICKETFILE_VERSION_MINOR"),
        readVersionPart(versionSource, "TICKETFILE_VERSION_PATCH")
    ].join(".");
    const packageData = JSON.parse(fs.readFileSync(packagePath, "utf8"));
    const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "ticketfile-vscode-"));
    const outputDirectory = path.join(repositoryDirectory, "build", "vscode");
    const outputPath = path.join(outputDirectory, "ticketfile-" + version + ".vsix");
    const npxCommand = process.platform === "win32" ? "npx.cmd" : "npx";
    let failed = false;

    try {
        packageData.version = version;
        writePackage(packageData, packagePath);
        fs.mkdirSync(outputDirectory, { recursive: true });

        fs.copyFileSync(path.join(extensionDirectory, ".vscodeignore"), path.join(temporaryDirectory, ".vscodeignore"));
        fs.copyFileSync(path.join(extensionDirectory, "extension.js"), path.join(temporaryDirectory, "extension.js"));
        fs.copyFileSync(path.join(extensionDirectory, "README.md"), path.join(temporaryDirectory, "README.md"));
        fs.copyFileSync(path.join(extensionDirectory, "ticket_parser.js"), path.join(temporaryDirectory, "ticket_parser.js"));
        fs.copyFileSync(path.join(repositoryDirectory, "LICENSE"), path.join(temporaryDirectory, "LICENSE"));
        copyDirectory(path.join(extensionDirectory, "resources"), path.join(temporaryDirectory, "resources"));
        writePackage(packageData, path.join(temporaryDirectory, "package.json"));

        const result = childProcess.spawnSync(
            npxCommand,
            ["--yes", "@vscode/vsce", "package", "--out", outputPath],
            { cwd: temporaryDirectory, stdio: "inherit" }
        );

        if (result.error) {
            throw result.error;
        }
        if (result.status !== 0) {
            failed = true;
        }
    } finally {
        try {
            fs.rmSync(temporaryDirectory, { recursive: true, force: true });
        } catch (error) {
            console.error("Cannot remove temporary directory: " + temporaryDirectory);
            console.error(error.message);
            failed = true;
        }
    }

    if (failed) {
        process.exitCode = 1;
    }
}

try {
    main();
} catch (error) {
    console.error(error.message);
    process.exitCode = 1;
}
