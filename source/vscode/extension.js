const vscode = require("vscode");

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

        const ticketsFolder = vscode.Uri.joinPath(workspaceFolders[0].uri, "tickets");
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

    context.subscriptions.push(ticketProvider.changeTreeDataEmitter, registration, refreshCommand);
}

function deactivate()
{
}

module.exports = {
    activate,
    deactivate
};
