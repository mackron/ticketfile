const vscode = require("vscode");

class TicketProvider
{
    getTreeItem(element)
    {
        return element;
    }

    getChildren()
    {
        return [];
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
