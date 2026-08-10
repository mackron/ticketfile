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

module.exports = {
    findStatusRange,
    parseTicket
};
