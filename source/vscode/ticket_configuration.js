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

module.exports = {
    validateStatusGroups
};
