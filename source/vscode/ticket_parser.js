function findStatusRange(text)
{
    const ticket = parseTicket(text);
    if (!ticket.hasMetadataSection) {
        return undefined;
    }

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

function findMetadataLines(text, key)
{
    const ranges = [];
    let cursor = 0;

    while (cursor < text.length) {
        let lineEnd = text.indexOf("\n", cursor);
        if (lineEnd === -1) {
            lineEnd = text.length;
        }

        const line = text.substring(cursor, lineEnd);
        if (line.trim() === "---") {
            break;
        }

        const match = /^([ \t\r]*)([^:]+?)([ \t]*:[ \t]*)(.*?)([ \t\r]*)$/.exec(line);
        if (match !== null && match[2].trim() === key) {
            ranges.push({
                lineOffset: cursor,
                lineLength: lineEnd - cursor + (lineEnd < text.length ? 1 : 0),
                valueOffset: cursor + match[1].length + match[2].length + match[3].length,
                valueLength: match[4].length
            });
        }

        cursor = lineEnd + 1;
    }

    return ranges;
}

function parseTicket(text)
{
    const metadata = new Map();
    const lines = text.split("\n");
    const separatorIndex = lines.findIndex((line) => line.trim() === "---");
    let hasMetadataSection = separatorIndex !== -1;
    let title;

    if (hasMetadataSection) {
        let metadataCount = 0;

        for (let i = 0; i < separatorIndex; i += 1) {
            const trimmedLine = lines[i].trim();
            if (trimmedLine === "") {
                continue;
            }

            const match = /^([^:]+?)\s*:\s*(.*?)\s*$/.exec(trimmedLine);
            if (match === null || match[1].trim() === "") {
                hasMetadataSection = false;
                metadata.clear();
                break;
            }

            metadata.set(match[1].trim(), match[2]);
            metadataCount += 1;
        }

        if (metadataCount === 0) {
            hasMetadataSection = false;
        }
    }

    const mainSectionStart = hasMetadataSection ? separatorIndex + 1 : 0;
    for (let i = mainSectionStart; i < lines.length; i += 1) {
        const line = lines[i];
        const trimmedLine = line.trim();

        if (trimmedLine === "---") {
            break;
        }
        if (trimmedLine !== "") {
            title = trimmedLine;
            break;
        }
    }

    const status = metadata.get("status");
    return { status, title, metadata, hasMetadataSection };
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
    findMetadataLines,
    findStatusRange,
    parseTicket,
    parseMetadataFilters,
    ticketMatchesFilters
};
