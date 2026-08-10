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

function parseTicket(text)
{
    const metadata = new Map();
    let foundSeparator = false;
    let title;

    for (const line of text.split("\n")) {
        const trimmedLine = line.trim();

        if (!foundSeparator) {
            if (trimmedLine === "---") {
                foundSeparator = true;
                continue;
            }

            const match = /^([^:]+?)\s*:\s*(.*?)\s*$/.exec(trimmedLine);
            if (match !== null) {
                const key = match[1].trim();

                if (key !== "") {
                    metadata.set(key, match[2]);
                }
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

    const status = metadata.get("status");
    if (status === undefined || status === "") {
        return { diagnostic: "Status metadata is missing." };
    }

    if (status !== "open" && status !== "closed") {
        return { diagnostic: `Unknown status: ${status}.` };
    }

    if (title === undefined) {
        return { diagnostic: "Title is missing." };
    }

    return { status, title, metadata };
}

function parseMetadataFilters(text)
{
    const filters = [];
    const tokens = text.match(/"[^"]*"|\S+/g) || [];

    for (let token of tokens) {
        if (token.startsWith("\"") || token.endsWith("\"")) {
            if (!token.startsWith("\"") || !token.endsWith("\"") || token.length < 2) {
                continue;
            }
            token = token.substring(1, token.length - 1);
        }

        const colonOffset = token.indexOf(":");
        if (colonOffset === -1) {
            continue;
        }

        const key = token.substring(0, colonOffset).trim();
        const value = token.substring(colonOffset + 1).trim();
        if (key !== "" && value !== "") {
            filters.push({ key, value });
        }
    }

    return filters;
}

function ticketMatchesFilters(ticket, filters)
{
    return filters.every((filter) => ticket.metadata.get(filter.key) === filter.value);
}

module.exports = {
    findStatusRange,
    parseTicket,
    parseMetadataFilters,
    ticketMatchesFilters
};
