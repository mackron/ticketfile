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
    constructor(fileName, status)
    {
        const label = /^\d+$/.test(fileName) ? `#${fileName}` : fileName;

        super(label, vscode.TreeItemCollapsibleState.None);

        this.fileName = fileName;
        this.status = status;
    }
}

class TicketProvider
{
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

            let status = "invalid";

            if (/^\d+$/.test(fileName)) {
                try {
                    const fileUri = vscode.Uri.joinPath(ticketsFolder, fileName);
                    const fileData = await vscode.workspace.fs.readFile(fileUri);

                    status = this.parseStatus(Buffer.from(fileData).toString("utf8"));
                } catch (error) {
                    status = "invalid";
                }
            }

            tickets.push(new TicketItem(fileName, status));
        }

        tickets.sort((ticketA, ticketB) => ticketA.fileName.localeCompare(
            ticketB.fileName,
            undefined,
            { numeric: true, sensitivity: "base" }
        ));

        return tickets;
    }

    parseStatus(text)
    {
        let status;

        for (const line of text.split("\n")) {
            const trimmedLine = line.trim();

            if (trimmedLine === "---") {
                return status === "open" || status === "closed" ? status : "invalid";
            }

            const match = /^status\s*:\s*(.*?)\s*$/.exec(trimmedLine);
            if (match !== null) {
                status = match[1];
            }
        }

        return "invalid";
    }
}

function activate(context)
{
    const ticketProvider = new TicketProvider();
    const registration = vscode.window.registerTreeDataProvider("ticketfile.tickets", ticketProvider);

    context.subscriptions.push(registration);
}

function deactivate()
{
}

module.exports = {
    activate,
    deactivate
};
