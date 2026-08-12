const vscode = require("vscode");
const { execFile } = require("child_process");
const { findStatusRange, parseMetadataFilters, parseTicket, ticketMatchesFilters } = require("./ticket_parser");

const defaultStatusGroups = [
    { label: "Open", status: "open", expanded: true },
    { label: "Closed", status: "closed", expanded: false }
];
const defaultTicketFolders = [
    { label: "Tickets", path: "tickets" }
];

function normalizeTicketFolderPath(folderPath)
{
    return folderPath.trim().replace(/\\/g, "/").replace(/\/+$/, "");
}

function validateTicketFolders(folders)
{
    const paths = new Set();

    if (!Array.isArray(folders) || folders.length === 0) {
        return "Ticket folders must be a non-empty array.";
    }

    for (let index = 0; index < folders.length; index += 1) {
        const folder = folders[index];
        const folderName = `Ticket folder ${index + 1}`;

        if (folder === null || typeof folder !== "object" || Array.isArray(folder)) {
            return `${folderName} must be an object.`;
        }
        if (typeof folder.label !== "string" || folder.label.trim() === "") {
            return `${folderName} must have a non-empty label.`;
        }
        if (typeof folder.path !== "string" || normalizeTicketFolderPath(folder.path) === "") {
            return `${folderName} must have a non-empty path.`;
        }

        const normalizedPath = normalizeTicketFolderPath(folder.path);
        if (paths.has(normalizedPath)) {
            return `Ticket folders contain duplicate path: ${normalizedPath}.`;
        }

        paths.add(normalizedPath);
    }

    return undefined;
}

function loadTicketFolders(configuration)
{
    const folders = configuration.get("ticketFolders", defaultTicketFolders);
    const error = validateTicketFolders(folders);
    const selectedFolders = error === undefined ? folders : defaultTicketFolders;

    return {
        error,
        folders: selectedFolders.map((folder) => ({
            label: folder.label.trim(),
            path: normalizeTicketFolderPath(folder.path)
        }))
    };
}

function validateStatusGroups(groups)
{
    const statuses = new Set();

    if (!Array.isArray(groups)) {
        return "Ticket status groups must be an array.";
    }

    for (let index = 0; index < groups.length; index += 1) {
        const group = groups[index];
        const groupName = `Ticket status group ${index + 1}`;

        if (group === null || typeof group !== "object" || Array.isArray(group)) {
            return `${groupName} must be an object.`;
        }
        if (typeof group.label !== "string" || group.label.trim() === "") {
            return `${groupName} must have a non-empty label.`;
        }
        if (typeof group.status !== "string" || group.status.trim() === "") {
            return `${groupName} must have a non-empty status.`;
        }
        if (typeof group.expanded !== "boolean") {
            return `${groupName} must have a Boolean expanded value.`;
        }
        if (statuses.has(group.status)) {
            return `Ticket status groups contain duplicate status: ${group.status}.`;
        }

        statuses.add(group.status);
    }

    return undefined;
}

function loadStatusGroups(configuration)
{
    const inspected = configuration.inspect("statusGroups");
    let groups;

    if (inspected !== undefined && inspected.workspaceValue !== undefined) {
        groups = inspected.workspaceValue;
    } else if (inspected !== undefined && inspected.globalValue !== undefined) {
        groups = inspected.globalValue;
    } else if (inspected !== undefined && inspected.defaultValue !== undefined) {
        groups = inspected.defaultValue;
    } else {
        groups = defaultStatusGroups;
    }

    const error = validateStatusGroups(groups);

    return {
        error,
        groups: error === undefined ? groups.map((group) => ({ ...group })) : defaultStatusGroups.map((group) => ({ ...group }))
    };
}

function isConfiguredStatus(status, groups)
{
    return groups.some((group) => group.status === status);
}

function readGitConfig(key, workingDirectory)
{
    return new Promise((resolve) => {
        execFile("git", ["config", "--get", key], { cwd: workingDirectory }, (error, stdout) => {
            if (error !== null) {
                resolve(undefined);
                return;
            }

            const value = stdout.trim();

            resolve(value === "" ? undefined : value);
        });
    });
}

async function getCommentAuthor()
{
    const environmentAuthor = process.env.TICKET_AUTHOR;
    if (environmentAuthor !== undefined && environmentAuthor.trim() !== "") {
        return environmentAuthor.trim();
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders !== undefined && workspaceFolders.length > 0) {
        const workingDirectory = workspaceFolders[0].uri.fsPath;
        const name = await readGitConfig("user.name", workingDirectory);

        if (name !== undefined) {
            return name;
        }

        const email = await readGitConfig("user.email", workingDirectory);
        if (email !== undefined) {
            return email;
        }
    }

    return "<Insert Name>";
}

function getCurrentDate()
{
    const date = new Date();
    const year = date.getFullYear().toString().padStart(4, "0");
    const month = (date.getMonth() + 1).toString().padStart(2, "0");
    const day = date.getDate().toString().padStart(2, "0");

    return `${year}-${month}-${day}`;
}

async function selectTicketFolder(ticketFolders, selectedItem)
{
    if (selectedItem !== undefined && selectedItem !== null && selectedItem.ticketFolder !== undefined) {
        const selectedFolder = ticketFolders.find((folder) => folder.path === selectedItem.ticketFolder.path);

        if (selectedFolder !== undefined) {
            return selectedFolder;
        }
    }

    if (ticketFolders.length === 1) {
        return ticketFolders[0];
    }

    const selected = await vscode.window.showQuickPick(
        ticketFolders.map((folder) => ({
            label: folder.label,
            description: folder.path,
            ticketFolder: folder
        })),
        {
            title: "Select Ticket Folder",
            placeHolder: "Select a folder for the new ticket"
        }
    );

    return selected === undefined || selected === null ? undefined : selected.ticketFolder;
}

async function createTicket(ticketFolder)
{
    const workspaceFolders = vscode.workspace.workspaceFolders;

    if (workspaceFolders === undefined || workspaceFolders.length === 0) {
        vscode.window.showErrorMessage("Open a workspace folder before creating a ticket.");
        return;
    }

    if (ticketFolder === undefined) {
        return;
    }

    const ticketsFolder = vscode.Uri.joinPath(workspaceFolders[0].uri, ticketFolder.path);

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

        const initialStatus = vscode.workspace.getConfiguration("ticketfile").get("initialStatus", "open").trim();
        const ticketTemplate = initialStatus === "" ? "---\n\n" : `status: ${initialStatus}\n\n---\n\n`;
        const ticketContents = Buffer.from(ticketTemplate, "utf8");
        let newID = foundID ? highestID + 1n : 1n;
        let ticketUri;

        for (;;) {
            ticketUri = vscode.Uri.joinPath(ticketsFolder, newID.toString());

            /* Create the complete ticket in one exclusive edit so another extension instance cannot overwrite this ID. */
            const createEdit = new vscode.WorkspaceEdit();
            createEdit.createFile(ticketUri, {
                overwrite: false,
                ignoreIfExists: false,
                contents: ticketContents
            });

            if (await vscode.workspace.applyEdit(createEdit)) {
                break;
            }

            try {
                await vscode.workspace.fs.stat(ticketUri);
                newID += 1n;
            } catch (error) {
                throw new Error("VS Code could not create the ticket file.");
            }
        }

        const document = await vscode.workspace.openTextDocument(ticketUri);
        const editor = await vscode.window.showTextDocument(document);
        const titlePosition = document.positionAt(document.getText().length);

        editor.selection = new vscode.Selection(titlePosition, titlePosition);
        editor.revealRange(new vscode.Range(titlePosition, titlePosition));
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to create ticket. ${error.message}`);
    }
}

async function updateTicketStatus(ticketItem, status, ticketProvider)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket before changing its status.");
        return;
    }

    try {
        const openDocument = vscode.workspace.textDocuments.find((document) =>
            document.uri.toString() === ticketItem.resourceUri.toString()
        );

        if (openDocument !== undefined && openDocument.isDirty) {
            vscode.window.showErrorMessage("Save ticket changes before changing its status.");
            return;
        }

        const fileData = await vscode.workspace.fs.readFile(ticketItem.resourceUri);
        const text = Buffer.from(fileData).toString("utf8");
        const statusRange = findStatusRange(text);
        const oldStatus = statusRange === undefined ? undefined : text.substring(statusRange.offset, statusRange.offset + statusRange.length);

        if (oldStatus === status) {
            return;
        }

        let updatedText;

        if (statusRange !== undefined) {
            updatedText = text.substring(0, statusRange.offset) + status + text.substring(statusRange.offset + statusRange.length);
        } else if (parseTicket(text).hasMetadataSection) {
            updatedText = `status: ${status}\n${text}`;
        } else {
            updatedText = `status: ${status}\n\n---\n\n${text}`;
        }

        let author;

        if (oldStatus !== undefined) {
            author = await getCommentAuthor();
            const separator = updatedText.endsWith("\n") ? "\n---\n\n" : "\n\n---\n\n";

            updatedText += `${separator}${getCurrentDate()} - ${author}\n\nStatus changed from ${oldStatus} to ${status}.\n`;
        }

        await vscode.workspace.fs.writeFile(ticketItem.resourceUri, Buffer.from(updatedText, "utf8"));
        ticketProvider.refresh();

        if (oldStatus !== undefined && author === "<Insert Name>") {
            vscode.window.showWarningMessage("Status was changed without an author name.");
        }
    } catch (error) {
        vscode.window.showErrorMessage(`Failed to update ticket. ${error.message}`);
    }
}

async function setTicketStatus(ticketItem, ticketProvider)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket before changing its status.");
        return;
    }

    const selected = await vscode.window.showQuickPick(
        ticketProvider.statusGroups.map((group) => ({
            label: group.label,
            description: group.status,
            status: group.status
        })),
        {
            title: "Set Ticket Status",
            placeHolder: "Select a status"
        }
    );

    if (selected !== undefined) {
        await updateTicketStatus(ticketItem, selected.status, ticketProvider);
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

async function commentOnTicket(ticketItem)
{
    if (!(ticketItem instanceof TicketItem)) {
        vscode.window.showErrorMessage("Select a ticket before adding a comment.");
        return;
    }

    try {
        const author = await getCommentAuthor();
        const document = await vscode.workspace.openTextDocument(ticketItem.resourceUri);
        const editor = await vscode.window.showTextDocument(document);
        const ticketText = document.getText();
        const separator = ticketText.endsWith("\n") ? "\n---\n\n" : "\n\n---\n\n";
        const commentTemplate = `${separator}${getCurrentDate()} - ${author}\n\n`;
        const insertionPosition = document.positionAt(ticketText.length);
        const inserted = await editor.edit((editBuilder) => {
            editBuilder.insert(insertionPosition, commentTemplate);
        });

        if (!inserted) {
            vscode.window.showErrorMessage("Failed to insert comment template.");
            return;
        }

        const commentPosition = document.positionAt(ticketText.length + commentTemplate.length);

        editor.selection = new vscode.Selection(commentPosition, commentPosition);
        editor.revealRange(new vscode.Range(commentPosition, commentPosition));

        if (author === "<Insert Name>") {
            vscode.window.showWarningMessage("No author name was found. Update <Insert Name> before saving.");
        }
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
    constructor(label, status, count, collapsibleState, ticketFolder, isFallback = false)
    {
        super(label, collapsibleState);

        this.status = status;
        this.ticketFolder = ticketFolder;
        this.isFallback = isFallback;
        this.description = count.toString();
    }
}

class TicketFolder extends vscode.TreeItem
{
    constructor(ticketFolder)
    {
        super(ticketFolder.label, vscode.TreeItemCollapsibleState.Expanded);

        this.ticketFolder = ticketFolder;
        this.contextValue = "ticketfileTicketFolder";
        this.tooltip = ticketFolder.path;
    }
}

class TicketItem extends vscode.TreeItem
{
    constructor(fileName, fileUri, status, title, diagnostic, ticketFolder)
    {
        let label = fileName;

        if (/^\d+$/.test(fileName)) {
            label = title === undefined ? `#${fileName}` : `#${fileName} - ${title}`;
        }

        super(label, vscode.TreeItemCollapsibleState.None);

        this.fileName = fileName;
        this.status = status;
        this.ticketFolder = ticketFolder;
        this.contextValue = "ticketfileTicket";
        this.resourceUri = fileUri;
        this.tooltip = diagnostic === undefined ? label : `${label}: ${diagnostic}`;
        this.command = {
            command: "ticketfile.openTicket",
            title: "Open Ticket",
            arguments: [this]
        };
    }
}

class TicketProvider
{
    constructor(ticketFolders, statusGroups)
    {
        this.changeTreeDataEmitter = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.changeTreeDataEmitter.event;
        this.filterText = "";
        this.filters = [];
        this.ticketFolders = ticketFolders;
        this.statusGroups = statusGroups;
        this.ticketPromises = new Map();
    }

    refresh()
    {
        this.ticketPromises.clear();
        this.changeTreeDataEmitter.fire(undefined);
    }

    setStatusGroups(statusGroups)
    {
        this.statusGroups = statusGroups;
        this.refresh();
    }

    setTicketFolders(ticketFolders)
    {
        this.ticketFolders = ticketFolders;
        this.refresh();
    }

    setFilter(filterText)
    {
        this.filterText = filterText;
        this.filters = parseMetadataFilters(filterText);
        this.refresh();
    }

    getTreeItem(element)
    {
        return element;
    }

    async getStatusGroups(ticketFolder)
    {
        const tickets = await this.getTickets(ticketFolder);
        const groups = this.statusGroups.map((group) => new TicketGroup(
            group.label,
            group.status,
            tickets.filter((ticket) => ticket.status === group.status).length,
            group.expanded ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.Collapsed,
            ticketFolder
        ));

        groups.push(new TicketGroup(
            "Uncategorized",
            undefined,
            tickets.filter((ticket) => !isConfiguredStatus(ticket.status, this.statusGroups)).length,
            vscode.TreeItemCollapsibleState.Collapsed,
            ticketFolder,
            true
        ));

        return groups;
    }

    async getChildren(element)
    {
        if (element === undefined) {
            if (this.ticketFolders.length > 1) {
                return this.ticketFolders.map((ticketFolder) => new TicketFolder(ticketFolder));
            }

            return this.getStatusGroups(this.ticketFolders[0]);
        }

        if (element instanceof TicketFolder) {
            return this.getStatusGroups(element.ticketFolder);
        }

        if (element instanceof TicketGroup) {
            const tickets = await this.getTickets(element.ticketFolder);
            if (element.isFallback) {
                return tickets.filter((ticket) => !isConfiguredStatus(ticket.status, this.statusGroups));
            }

            return tickets.filter((ticket) => ticket.status === element.status);
        }

        return [];
    }

    async getTickets(ticketFolder)
    {
        if (!this.ticketPromises.has(ticketFolder.path)) {
            this.ticketPromises.set(ticketFolder.path, this.loadTickets(ticketFolder));
        }

        return this.ticketPromises.get(ticketFolder.path);
    }

    async loadTickets(ticketFolder)
    {
        const workspaceFolders = vscode.workspace.workspaceFolders;
        const tickets = [];

        if (workspaceFolders === undefined || workspaceFolders.length === 0) {
            return tickets;
        }

        const ticketsFolder = vscode.Uri.joinPath(workspaceFolders[0].uri, ticketFolder.path);
        let entries;

        try {
            entries = await vscode.workspace.fs.readDirectory(ticketsFolder);
        } catch (error) {
            return tickets;
        }

        for (const [fileName, fileType] of entries) {
            if (fileType !== vscode.FileType.File || fileName.includes(".ticketfile-tmp-")) {
                continue;
            }

            const fileUri = vscode.Uri.joinPath(ticketsFolder, fileName);
            let status;
            let title;
            let diagnostic;

            if (/^\d+$/.test(fileName)) {
                try {
                    const fileData = await vscode.workspace.fs.readFile(fileUri);
                    const parsedTicket = parseTicket(Buffer.from(fileData).toString("utf8"));

                    if (!ticketMatchesFilters(parsedTicket, this.filters)) {
                        continue;
                    }

                    status = parsedTicket.status;
                    title = parsedTicket.title;
                } catch (error) {
                    diagnostic = "File could not be read.";
                }
            } else {
                diagnostic = "Filename must contain only a numeric ticket ID.";
            }

            if (this.filters.length > 0 && diagnostic !== undefined) {
                continue;
            }

            tickets.push(new TicketItem(fileName, fileUri, status, title, diagnostic, ticketFolder));
        }

        tickets.sort((ticketA, ticketB) => ticketA.fileName.localeCompare(
            ticketB.fileName,
            undefined,
            { numeric: true, sensitivity: "base" }
        ));

        return tickets;
    }

}

function activate(context)
{
    const ticketFolderConfiguration = loadTicketFolders(vscode.workspace.getConfiguration("ticketfile"));
    const statusGroupConfiguration = loadStatusGroups(vscode.workspace.getConfiguration("ticketfile"));

    if (ticketFolderConfiguration.error !== undefined) {
        vscode.window.showErrorMessage(ticketFolderConfiguration.error);
    }
    if (statusGroupConfiguration.error !== undefined) {
        vscode.window.showErrorMessage(statusGroupConfiguration.error);
    }

    const ticketProvider = new TicketProvider(ticketFolderConfiguration.folders, statusGroupConfiguration.groups);
    const treeView = vscode.window.createTreeView("ticketfile.tickets", { treeDataProvider: ticketProvider });

    function updateFilterDisplay()
    {
        const hasFilter = ticketProvider.filters.length > 0;

        treeView.description = hasFilter ? "Filtered" : undefined;
        treeView.message = hasFilter ? `Active filter: ${ticketProvider.filterText}` : undefined;
        vscode.commands.executeCommand("setContext", "ticketfile.hasFilter", hasFilter);
    }

    const refreshCommand = vscode.commands.registerCommand("ticketfile.refresh", () => {
        updateTicketWatchers();
        ticketProvider.refresh();
    });
    const filterCommand = vscode.commands.registerCommand("ticketfile.filterTickets", async () => {
        const filterText = await vscode.window.showInputBox({
            title: "Filter Tickets",
            prompt: "Example: status:open \"assignee:Clanker Bot\"",
            value: ticketProvider.filterText
        });

        if (filterText !== undefined) {
            ticketProvider.setFilter(filterText.trim());
            updateFilterDisplay();
        }
    });
    const clearFilterCommand = vscode.commands.registerCommand("ticketfile.clearFilter", () => {
        ticketProvider.setFilter("");
        updateFilterDisplay();
    });
    const createTicketCommand = vscode.commands.registerCommand("ticketfile.createTicket", async (selectedItem) => {
        const inferredItem = selectedItem === undefined || selectedItem === null ? treeView.selection[0] : selectedItem;
        const ticketFolder = await selectTicketFolder(ticketProvider.ticketFolders, inferredItem);

        await createTicket(ticketFolder);
        updateTicketWatchers();
        ticketProvider.refresh();
    });
    const openTicketCommand = vscode.commands.registerCommand("ticketfile.openTicket", openTicket);
    const addCommentCommand = vscode.commands.registerCommand("ticketfile.addComment", commentOnTicket);
    const setTicketStatusCommand = vscode.commands.registerCommand("ticketfile.setTicketStatus", (ticketItem) => {
        return setTicketStatus(ticketItem, ticketProvider);
    });
    const deleteTicketCommand = vscode.commands.registerCommand("ticketfile.deleteTicket", (ticketItem) => {
        return deleteTicket(ticketItem, ticketProvider);
    });

    context.subscriptions.push(ticketProvider.changeTreeDataEmitter, treeView, refreshCommand, filterCommand, clearFilterCommand, createTicketCommand, openTicketCommand, addCommentCommand, setTicketStatusCommand, deleteTicketCommand
    );

    updateFilterDisplay();

    let watcherDisposables = [];

    function updateTicketWatchers()
    {
        const workspaceFolders = vscode.workspace.workspaceFolders;

        for (const disposable of watcherDisposables) {
            disposable.dispose();
        }
        watcherDisposables = [];

        if (workspaceFolders === undefined || workspaceFolders.length === 0) {
            return;
        }

        for (const ticketFolder of ticketProvider.ticketFolders) {
            const ticketPattern = new vscode.RelativePattern(workspaceFolders[0], `${ticketFolder.path}/*`);
            const ticketWatcher = vscode.workspace.createFileSystemWatcher(ticketPattern);

            watcherDisposables.push(
                ticketWatcher.onDidCreate(() => ticketProvider.refresh()),
                ticketWatcher.onDidChange(() => ticketProvider.refresh()),
                ticketWatcher.onDidDelete(() => ticketProvider.refresh()),
                ticketWatcher
            );
        }
    }

    updateTicketWatchers();

    const configurationRegistration = vscode.workspace.onDidChangeConfiguration((event) => {
        if (event.affectsConfiguration("ticketfile.ticketFolders")) {
            const updatedConfiguration = loadTicketFolders(vscode.workspace.getConfiguration("ticketfile"));

            if (updatedConfiguration.error !== undefined) {
                vscode.window.showErrorMessage(updatedConfiguration.error);
            }

            ticketProvider.setTicketFolders(updatedConfiguration.folders);
            updateTicketWatchers();
        }

        if (event.affectsConfiguration("ticketfile.statusGroups")) {
            const updatedConfiguration = loadStatusGroups(vscode.workspace.getConfiguration("ticketfile"));

            if (updatedConfiguration.error !== undefined) {
                vscode.window.showErrorMessage(updatedConfiguration.error);
            }

            ticketProvider.setStatusGroups(updatedConfiguration.groups);
        } else if (event.affectsConfiguration("ticketfile.initialStatus")) {
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
