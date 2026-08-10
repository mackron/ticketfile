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

function findStatusRange(text)
{
    let cursor = 0;
    let statusRange;

    while (cursor < text.length) {
        let lineEnd = text.indexOf("\n", cursor);
        if (lineEnd === -1) {
            lineEnd = text.length;
        }

        const line = text.substring(cursor, lineEnd);
        if (line.trim() === "---") {
            return statusRange;
        }

        const match = /^([ \t\r]*status[ \t]*:[ \t]*)(.*?)([ \t\r]*)$/.exec(line);
        if (match !== null) {
            statusRange = {
                offset: cursor + match[1].length,
                length: match[2].length
            };
        }

        cursor = lineEnd + 1;
    }

    return undefined;
}

async function updateTicketStatus(ticketItem, status, ticketProvider)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket before changing its status.");
        return;
    }

    try {
        const fileData = await vscode.workspace.fs.readFile(ticketItem.resourceUri);
        const text = Buffer.from(fileData).toString("utf8");
        const statusRange = findStatusRange(text);

        if (statusRange === undefined) {
            vscode.window.showErrorMessage(`Ticket ${ticketItem.fileName} does not have valid status metadata.`);
            return;
        }

        const currentStatus = text.substring(statusRange.offset, statusRange.offset + statusRange.length);
        if (currentStatus === status) {
            return;
        }

        const updatedText = text.substring(0, statusRange.offset) + status + text.substring(statusRange.offset + statusRange.length);

        await vscode.workspace.fs.writeFile(ticketItem.resourceUri, Buffer.from(updatedText, "utf8"));
        ticketProvider.refresh();
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to update ticket. ${error.message}`);
    }
}

async function openTicket(ticketItem)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket to open.");
        return;
    }

    try {
        const document = await vscode.workspace.openTextDocument(ticketItem.resourceUri);

        await vscode.window.showTextDocument(document);
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to open ticket. ${error.message}`);
    }
}

async function deleteTicket(ticketItem, ticketProvider)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket to delete.");
        return;
    }

    const confirmation = await vscode.window.showWarningMessage(
        `Delete ${ticketItem.label}?`,
        { modal: true },
        "Move to Trash"
    );

    if (confirmation !== "Move to Trash") {
        return;
    }

    try {
        await vscode.workspace.fs.delete(ticketItem.resourceUri, {
            recursive: false,
            useTrash: true
        });
        ticketProvider.refresh();
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to delete ticket. ${error.message}`);
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
    constructor(fileName, fileUri, status, title, diagnostic)
    {
        let label = fileName;

        if (/^\d+$/.test(fileName)) {
            label = title === undefined ? `#${fileName}` : `#${fileName} ${title}`;
        }

        super(label, vscode.TreeItemCollapsibleState.None);

        this.fileName = fileName;
        this.status = status;
        this.contextValue = status === "open" || status === "closed" ? `${status}Ticket` : "invalidTicket";
        this.resourceUri = fileUri;
        this.tooltip = diagnostic === undefined ? label : `Invalid ticket: ${diagnostic}`;
        this.command = {
            command: "ticketfile.openTicket",
            title: "Open Ticket",
            arguments: [this]
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
            let diagnostic;

            if (/^\d+$/.test(fileName)) {
                try {
                    const fileData = await vscode.workspace.fs.readFile(fileUri);
                    const parsedTicket = this.parseTicket(Buffer.from(fileData).toString("utf8"));

                    if (parsedTicket.diagnostic === undefined) {
                        status = parsedTicket.status;
                        title = parsedTicket.title;
                    } else {
                        diagnostic = parsedTicket.diagnostic;
                    }
                } catch (error) {
                    diagnostic = "File could not be read.";
                }
            } else {
                diagnostic = "Filename must contain only a numeric ticket ID.";
            }

            tickets.push(new TicketItem(fileName, fileUri, status, title, diagnostic));
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
        let title;

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
                title = trimmedLine;
                break;
            }
        }

        if (!foundSeparator) {
            return { diagnostic: "Metadata separator is missing." };
        }

        if (status === undefined || status === "") {
            return { diagnostic: "Status metadata is missing." };
        }

        if (status !== "open" && status !== "closed") {
            return { diagnostic: `Unknown status: ${status}.` };
        }

        if (title === undefined) {
            return { diagnostic: "Title is missing." };
        }

        return { status, title };
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
    const openTicketCommand = vscode.commands.registerCommand("ticketfile.openTicket", openTicket);
    const closeTicketCommand = vscode.commands.registerCommand("ticketfile.closeTicket", (ticketItem) => {
        return updateTicketStatus(ticketItem, "closed", ticketProvider);
    });
    const reopenTicketCommand = vscode.commands.registerCommand("ticketfile.reopenTicket", (ticketItem) => {
        return updateTicketStatus(ticketItem, "open", ticketProvider);
    });
    const deleteTicketCommand = vscode.commands.registerCommand("ticketfile.deleteTicket", (ticketItem) => {
        return deleteTicket(ticketItem, ticketProvider);
    });

    context.subscriptions.push(ticketProvider.changeTreeDataEmitter, registration, refreshCommand, createTicketCommand, openTicketCommand, closeTicketCommand, reopenTicketCommand, deleteTicketCommand
    );

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
