const defaultStatusGroups = [
    { label: "Open", status: "open", expanded: true },
    { label: "Closed", status: "closed", expanded: false }
];

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
        groups: error === undefined ? groups.map((group) => ({ ...group })) :
            defaultStatusGroups.map((group) => ({ ...group }))
    };
}

module.exports = {
    loadStatusGroups,
    validateStatusGroups
};
