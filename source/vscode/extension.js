const vscode = require("vscode");

function getTicketsFolderPath()
{
    const configuredPath = vscode.workspace.getConfiguration("ticketfile").get("ticketsFolder", "tickets");

    return configuredPath.trim() === "" ? "tickets" : configuredPath;
}

async function createTicket()
{
    const workspaceFolders = vscode.workspace.workspaceFolders;

    if (workspaceFolders === undefined || workspaceFolders.length === 0) {
        vscode.window.showErrorMessage("Open a workspace folder before creating a ticket.");
        return;
    }

    const ticketsFolder = vscode.Uri.joinPath(workspaceFolders[0].uri, getTicketsFolderPath());

    try {
        await vscode.workspace.fs.createDirectory(ticketsFolder);

        const entries = await vscode.workspace.fs.readDirectory(ticketsFolder);
        let highestID = 0n;
        let foundID = false;

        for (const [fileName] of entries) {
            if (/^\d+$/.test(fileName)) {
                const id = BigInt(fileName);

                if (!foundID || id > highestID) {
                    highestID = id;
                    foundID = true;
                }
            }
        }

        const newID = foundID ? highestID + 1n : 1n;
        const ticketUri = vscode.Uri.joinPath(ticketsFolder, newID.toString());
        const ticketTemplate = "status: open\n\n---\n\n";

        await vscode.workspace.fs.writeFile(ticketUri, Buffer.from(ticketTemplate, "utf8"));

        const document = await vscode.workspace.openTextDocument(ticketUri);
        const editor = await vscode.window.showTextDocument(document);
        const titlePosition = document.positionAt(document.getText().length);

        editor.selection = new vscode.Selection(titlePosition, titlePosition);
        editor.revealRange(new vscode.Range(titlePosition, titlePosition));
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to create ticket. ${error.message}`);
    }
}

class TicketGroup extends vscode.TreeItem
{
    constructor(label, status, collapsibleState)
    {
        super(label, collapsibleState);

        this.status = status;
    }
}

class TicketItem extends vscode.TreeItem
{
    constructor(fileName, fileUri, status, title)
    {
        let label = fileName;

        if (/^\d+$/.test(fileName)) {
            label = title === undefined ? `#${fileName}` : `#${fileName} ${title}`;
        }

        super(label, vscode.TreeItemCollapsibleState.None);

        this.fileName = fileName;
        this.status = status;
        this.resourceUri = fileUri;
        this.command = {
            command: "vscode.open",
            title: "Open Ticket",
            arguments: [fileUri]
        };
    }
}

class TicketProvider
{
    constructor()
    {
        this.changeTreeDataEmitter = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.changeTreeDataEmitter.event;
    }

    refresh()
    {
        this.changeTreeDataEmitter.fire(undefined);
    }

    getTreeItem(element)
    {
        return element;
    }

    async getChildren(element)
    {
        if (element === undefined) {
            return [
                new TicketGroup("Open", "open", vscode.TreeItemCollapsibleState.Expanded),
                new TicketGroup("Closed", "closed", vscode.TreeItemCollapsibleState.Collapsed),
                new TicketGroup("Invalid", "invalid", vscode.TreeItemCollapsibleState.Collapsed)
            ];
        }

        if (element instanceof TicketGroup) {
            const tickets = await this.getTickets();

            return tickets.filter((ticket) => ticket.status === element.status);
        }

        return [];
    }

    async getTickets()
    {
        const workspaceFolders = vscode.workspace.workspaceFolders;
        const tickets = [];

        if (workspaceFolders === undefined || workspaceFolders.length === 0) {
            return tickets;
        }

        const ticketsFolder = vscode.Uri.joinPath(workspaceFolders[0].uri, getTicketsFolderPath());
        let entries;

        try {
            entries = await vscode.workspace.fs.readDirectory(ticketsFolder);
        } catch (error) {
            return tickets;
        }

        for (const [fileName, fileType] of entries) {
            if (fileType !== vscode.FileType.File) {
                continue;
            }

            const fileUri = vscode.Uri.joinPath(ticketsFolder, fileName);
            let status = "invalid";
            let title;

            if (/^\d+$/.test(fileName)) {
                try {
                    const fileData = await vscode.workspace.fs.readFile(fileUri);
                    const parsedTicket = this.parseTicket(Buffer.from(fileData).toString("utf8"));

                    if (parsedTicket !== undefined) {
                        status = parsedTicket.status;
                        title = parsedTicket.title;
                    }
                } catch (error) {
                    status = "invalid";
                }
            }

            tickets.push(new TicketItem(fileName, fileUri, status, title));
        }

        tickets.sort((ticketA, ticketB) => ticketA.fileName.localeCompare(
            ticketB.fileName,
            undefined,
            { numeric: true, sensitivity: "base" }
        ));

        return tickets;
    }

    parseTicket(text)
    {
        let status;
        let foundSeparator = false;

        for (const line of text.split("\n")) {
            const trimmedLine = line.trim();

            if (!foundSeparator) {
                if (trimmedLine === "---") {
                    foundSeparator = true;
                    continue;
                }

                const match = /^status\s*:\s*(.*?)\s*$/.exec(trimmedLine);
                if (match !== null) {
                    status = match[1];
                }

                continue;
            }

            if (trimmedLine !== "") {
                if (status !== "open" && status !== "closed") {
                    return undefined;
                }

                return {
                    status,
                    title: trimmedLine
                };
            }
        }

        return undefined;
    }
}

function activate(context)
{
    const ticketProvider = new TicketProvider();
    const registration = vscode.window.registerTreeDataProvider("ticketfile.tickets", ticketProvider);
    const refreshCommand = vscode.commands.registerCommand("ticketfile.refresh", () => {
        ticketProvider.refresh();
    });
    const createTicketCommand = vscode.commands.registerCommand("ticketfile.createTicket", createTicket);

    context.subscriptions.push(ticketProvider.changeTreeDataEmitter, registration, refreshCommand, createTicketCommand);

    let watcherDisposables = [];

    function updateTicketWatcher()
    {
        const workspaceFolders = vscode.workspace.workspaceFolders;

        for (const disposable of watcherDisposables) {
            disposable.dispose();
        }
        watcherDisposables = [];

        if (workspaceFolders !== undefined && workspaceFolders.length > 0) {
            const ticketsFolderPath = getTicketsFolderPath().replace(/\\/g, "/").replace(/\/+$/, "");
            const ticketPattern = new vscode.RelativePattern(workspaceFolders[0], `${ticketsFolderPath}/*`);
            const ticketWatcher = vscode.workspace.createFileSystemWatcher(ticketPattern);

            watcherDisposables.push(
                ticketWatcher.onDidCreate(() => ticketProvider.refresh()),
                ticketWatcher.onDidChange(() => ticketProvider.refresh()),
                ticketWatcher.onDidDelete(() => ticketProvider.refresh()),
                ticketWatcher
            );
        }
    }

    updateTicketWatcher();

    const configurationRegistration = vscode.workspace.onDidChangeConfiguration((event) => {
        if (event.affectsConfiguration("ticketfile.ticketsFolder")) {
            updateTicketWatcher();
            ticketProvider.refresh();
        }
    });

    context.subscriptions.push(configurationRegistration, new vscode.Disposable(() => {
        for (const disposable of watcherDisposables) {
            disposable.dispose();
        }
    }));
}

function deactivate()
{
}

module.exports = {
    activate,
    deactivate
};
