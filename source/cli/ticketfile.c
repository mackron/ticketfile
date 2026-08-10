#include <limits.h>
#include <stdlib.h>
#include <time.h>

#include "../../external/fs/fs.c"

static fs_file* STDIN  = NULL;
static fs_file* STDOUT = NULL;
static fs_file* STDERR = NULL;

static const char* g_pTicketsFolder = "tickets";

static void print_usage(const char* executablePath)
{
    fs_file_writef(STDOUT, "Usage:\n");
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] list [--status <open|closed>]\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] show <id>\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] edit <id>\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] close <id> [--no-comment]\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] reopen <id> [--no-comment]\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] new [-m <message> | --message <message>]\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] comment <id>\n", executablePath);
    fs_file_writef(STDOUT, "  %s --test [case]\n", executablePath);
}


/* BEG parser */
typedef struct
{
    size_t offset;
    size_t length;
} text_range;

typedef struct
{
    text_range status;
    text_range shortDescription;
} ticket;

static int is_horizontal_whitespace(char character)
{
    return character == ' ' || character == '\t' || character == '\r';
}

static text_range trim_line(const char* pText, size_t lineOffset, size_t lineLength)
{
    text_range range;

    while (lineLength > 0 && is_horizontal_whitespace(pText[lineOffset])) {
        lineOffset += 1;
        lineLength -= 1;
    }

    while (lineLength > 0 && is_horizontal_whitespace(pText[lineOffset + lineLength - 1])) {
        lineLength -= 1;
    }

    range.offset = lineOffset;
    range.length = lineLength;

    return range;
}

static int text_range_equal(const char* pText, text_range range, const char* pValue)
{
    size_t valueLength = strlen(pValue);

    return range.length == valueLength && memcmp(pText + range.offset, pValue, valueLength) == 0;
}

static int parse_ticket(const char* pText, size_t textLength, ticket* pTicket)
{
    size_t cursor = 0;
    int foundSeparator = 0;

    pTicket->status.offset = 0;
    pTicket->status.length = 0;
    pTicket->shortDescription.offset = 0;
    pTicket->shortDescription.length = 0;

    while (cursor < textLength) {
        text_range line;
        size_t lineOffset = cursor;
        size_t colonOffset;

        while (cursor < textLength && pText[cursor] != '\n') {
            cursor += 1;
        }

        line = trim_line(pText, lineOffset, cursor - lineOffset);
        if (cursor < textLength) {
            cursor += 1;
        }

        if (line.length == 3 && memcmp(pText + line.offset, "---", 3) == 0) {
            foundSeparator = 1;
            break;
        }

        colonOffset = 0;
        while (colonOffset < line.length && pText[line.offset + colonOffset] != ':') {
            colonOffset += 1;
        }

        if (colonOffset < line.length) {
            text_range key;
            text_range value;

            key = trim_line(pText, line.offset, colonOffset);
            value = trim_line(pText, line.offset + colonOffset + 1, line.length - colonOffset - 1);

            if (key.length == 6 && memcmp(pText + key.offset, "status", 6) == 0) {
                pTicket->status = value;
            }
        }
    }

    if (!foundSeparator) {
        return 0;
    }

    while (cursor < textLength) {
        text_range line;
        size_t lineOffset = cursor;

        while (cursor < textLength && pText[cursor] != '\n') {
            cursor += 1;
        }

        line = trim_line(pText, lineOffset, cursor - lineOffset);
        if (cursor < textLength) {
            cursor += 1;
        }

        if (line.length > 0) {
            pTicket->shortDescription = line;
            break;
        }
    }

    if (pTicket->shortDescription.length == 0) {
        return 0;
    }

    return text_range_equal(pText, pTicket->status, "open") || text_range_equal(pText, pTicket->status, "closed");
}

static char* replace_text_range(const char* pText, size_t textLength, text_range range, const char* pReplacement, size_t replacementLength, size_t* pUpdatedTextLength)
{
    char* pUpdatedText;
    char* pWriteCursor;
    size_t prefixLength;
    size_t suffixOffset;
    size_t suffixLength;

    if (range.offset > textLength || range.length > textLength - range.offset) {
        return NULL;
    }

    prefixLength = range.offset;
    suffixOffset = range.offset + range.length;
    suffixLength = textLength - suffixOffset;
    *pUpdatedTextLength = prefixLength + replacementLength + suffixLength;

    pUpdatedText = (char*)malloc(*pUpdatedTextLength);
    if (pUpdatedText == NULL) {
        return NULL;
    }

    pWriteCursor = pUpdatedText;

    memcpy(pWriteCursor, pText, prefixLength);
    pWriteCursor += prefixLength;

    memcpy(pWriteCursor, pReplacement, replacementLength);
    pWriteCursor += replacementLength;

    memcpy(pWriteCursor, pText + suffixOffset, suffixLength);

    return pUpdatedText;
}
/* END parser */


static char* get_ticket_path(const char* pID, size_t idLength)
{
    char* pFilePath;
    int filePathLength;

    filePathLength = fs_path_append(NULL, 0, g_pTicketsFolder, FS_NULL_TERMINATED, pID, idLength);
    if (filePathLength < 0) {
        return NULL;
    }

    pFilePath = (char*)malloc((size_t)filePathLength + 1);
    if (pFilePath == NULL) {
        return NULL;
    }

    fs_path_append(pFilePath, (size_t)filePathLength + 1, g_pTicketsFolder, FS_NULL_TERMINATED, pID, idLength);

    return pFilePath;
}

static fs_result read_text_file(const char* pFilePath, char** ppFileData, size_t* pFileDataSize)
{
    fs_result result;
    fs_file* pFile;

    result = fs_file_open(NULL, pFilePath, FS_READ, &pFile);
    if (result != FS_SUCCESS) {
        return result;
    }

    result = fs_file_read_to_end(pFile, FS_FORMAT_TEXT, (void**)ppFileData, pFileDataSize);
    fs_file_close(pFile);

    return result;
}

static fs_result write_text_file(const char* pFilePath, const void* pFileData, size_t fileDataSize)
{
    return fs_file_open_and_write(NULL, pFilePath, pFileData, fileDataSize);
}

static int run_text_editor(const char* pFilePath)
{
    const char* pEditor;
    char* pCommand;
    size_t editorLength;
    size_t filePathLength;
    size_t commandCapacity;
    size_t commandLength;
    size_t i;
    int result;

    pEditor = getenv("VISUAL");
    if (pEditor == NULL || pEditor[0] == '\0') {
        pEditor = getenv("EDITOR");
    }

    if (pEditor == NULL || pEditor[0] == '\0') {
        #if defined(FS_WIN32)
        {
            pEditor = "notepad";
        }
        #else
        {
            pEditor = "vi";
        }
        #endif
    }

    editorLength   = strlen(pEditor);
    filePathLength = strlen(pFilePath);

    /* Allow four bytes per path character for POSIX escaping, plus a space, two quotes, a null terminator, and one spare byte. */
    commandCapacity = editorLength + 2 + filePathLength * 4 + 3;
    pCommand = (char*)malloc(commandCapacity);
    if (pCommand == NULL) {
        return 0;
    }

    memcpy(pCommand, pEditor, editorLength);
    commandLength = editorLength;
    pCommand[commandLength++] = ' ';

    #if defined(FS_WIN32)
    {
        pCommand[commandLength++] = '"';
        {
            for (i = 0; i < filePathLength; i += 1) {
                if (pFilePath[i] == '"') {
                    pCommand[commandLength++] = '\\';
                }

                pCommand[commandLength++] = pFilePath[i];
            }
        }
        pCommand[commandLength++] = '"';
    }
    #else
    {
        pCommand[commandLength++] = '\'';
        {
            for (i = 0; i < filePathLength; i += 1) {
                if (pFilePath[i] == '\'') {
                    memcpy(pCommand + commandLength, "'\\''", 4);
                    commandLength += 4;
                } else {
                    pCommand[commandLength++] = pFilePath[i];
                }
            }
        }
        pCommand[commandLength++] = '\'';
    }
    #endif

    pCommand[commandLength] = '\0';
    result = system(pCommand);

    return result == 0;
}

static int edit_text(const char* pText, size_t textLength, char** ppEditedText, size_t* pEditedTextLength)
{
    fs_result result;
    char temporaryPath[1024];

    result = fs_mktmp("ticket", temporaryPath, sizeof(temporaryPath), FS_MKTMP_FILE);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to create a temporary ticket. %s.\n", fs_result_description(result));
        return 0;
    }

    result = write_text_file(temporaryPath, pText, textLength);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to write temporary ticket. %s.\n", fs_result_description(result));
        fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
        return 0;
    }

    if (!run_text_editor(temporaryPath)) {
        fs_file_writef(STDERR, "Text editor failed.\n");
        fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
        return 0;
    }

    result = read_text_file(temporaryPath, ppEditedText, pEditedTextLength);
    fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to read temporary ticket. %s.\n", fs_result_description(result));
        return 0;
    }

    return 1;
}

static int read_command_line(const char* pCommand, char* pOutput, size_t outputCapacity)
{
    FILE* pPipe;
    size_t outputLength;
    int result;

    if (outputCapacity == 0) {
        return 0;
    }

    #if defined(FS_WIN32)
        pPipe = _popen(pCommand, "r");
    #else
        pPipe = popen(pCommand, "r");
    #endif
    if (pPipe == NULL) {
        return 0;
    }

    if (fgets(pOutput, (int)outputCapacity, pPipe) == NULL) {
        pOutput[0] = '\0';
    }

    #if defined(FS_WIN32)
        result = _pclose(pPipe);
    #else
        result = pclose(pPipe);
    #endif

    outputLength = strlen(pOutput);
    while (outputLength > 0 && (pOutput[outputLength - 1] == '\n' || pOutput[outputLength - 1] == '\r')) {
        outputLength -= 1;
    }
    pOutput[outputLength] = '\0';

    return result == 0 && outputLength > 0;
}

static int copy_string(char* pDestination, size_t destinationCapacity, const char* pSource)
{
    size_t sourceLength = strlen(pSource);

    if (sourceLength >= destinationCapacity) {
        return 0;
    }

    memcpy(pDestination, pSource, sourceLength + 1);
    return 1;
}

static int get_comment_author(char* pAuthor, size_t authorCapacity)
{
    const char* pEnvironmentAuthor = getenv("TICKET_AUTHOR");

    if (pEnvironmentAuthor != NULL && pEnvironmentAuthor[0] != '\0' &&
        copy_string(pAuthor, authorCapacity, pEnvironmentAuthor)) {
        return 1;
    }

    if (read_command_line("git config --get user.name", pAuthor, authorCapacity)) {
        return 1;
    }

    if (read_command_line("git config --get user.email", pAuthor, authorCapacity)) {
        return 1;
    }

    copy_string(pAuthor, authorCapacity, "<Insert Name>");
    return 0;
}

static char* create_comment_header(size_t* pHeaderLength, int* pAuthorFound)
{
    char author[256];
    char date[11];
    size_t authorLength;
    char* pHeader;
    time_t currentTime;
    struct tm* pLocalTime;

    *pAuthorFound = get_comment_author(author, sizeof(author));

    currentTime = time(NULL);
    pLocalTime = localtime(&currentTime);
    if (pLocalTime == NULL || strftime(date, sizeof(date), "%Y-%m-%d", pLocalTime) == 0) {
        fs_file_writef(STDERR, "Failed to determine current date.\n");
        return NULL;
    }

    authorLength = strlen(author);
    *pHeaderLength = 10 + 3 + authorLength;
    pHeader = (char*)malloc(*pHeaderLength + 1);
    if (pHeader == NULL) {
        fs_file_writef(STDERR, "Failed to allocate comment header.\n");
        return NULL;
    }

    memcpy(pHeader, date, 10);
    memcpy(pHeader + 10, " - ", 3);
    memcpy(pHeader + 13, author, authorLength);
    pHeader[*pHeaderLength] = '\0';

    return pHeader;
}

static int parse_ticket_id(const char* pID, size_t idLength, unsigned long* pValue)
{
    unsigned long value = 0;
    size_t i;

    if (idLength == 0) {
        return 0;
    }

    for (i = 0; i < idLength; i += 1) {
        unsigned int digit;

        if (pID[i] < '0' || pID[i] > '9') {
            return 0;
        }

        digit = (unsigned int)(pID[i] - '0');
        if (value > (ULONG_MAX - digit) / 10) {
            return 0;
        }

        value = value * 10 + digit;
    }

    *pValue = value;
    return 1;
}

static int create_ticket_file(const char* pFileData, size_t fileDataSize)
{
    fs_iterator* pIterator;
    unsigned long highestID = 0;
    int foundID = 0;
    unsigned long newID;

    for (pIterator = fs_first(NULL, g_pTicketsFolder, 0); pIterator != NULL; pIterator = fs_next(pIterator)) {
        unsigned long id;

        if (parse_ticket_id(pIterator->pName, pIterator->nameLen, &id)) {
            if (!foundID || id > highestID) {
                highestID = id;
                foundID = 1;
            }
        }
    }

    if (foundID && highestID == ULONG_MAX) {
        fs_file_writef(STDERR, "No ticket ID is available.\n");
        return 1;
    }

    newID = foundID ? highestID + 1 : 1;    /* Start at 1. We'll reserve ticket 0 as a special one. */
    for (;;) {
        fs_result result;
        fs_file* pFile;
        char id[3 * sizeof(unsigned long) + 1];
        char* pFilePath;

        fs_sprintf(id, "%lu", newID);

        pFilePath = get_ticket_path(id, FS_NULL_TERMINATED);
        if (pFilePath == NULL) {
            fs_file_writef(STDERR, "Failed to construct ticket path.\n");
            return 1;
        }

        result = fs_file_open(NULL, pFilePath, FS_WRITE | FS_EXCLUSIVE, &pFile);
        if (result == FS_ALREADY_EXISTS) {
            if (newID == ULONG_MAX) {
                fs_file_writef(STDERR, "No ticket ID is available.\n");
                return 1;
            }

            newID += 1;
            continue;
        }

        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to create %s. %s.\n", pFilePath, fs_result_description(result));
            return 1;
        }

        result = fs_file_write(pFile, pFileData, fileDataSize, NULL);
        fs_file_close(pFile);

        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to write %s. %s.\n", pFilePath, fs_result_description(result));
            fs_remove(NULL, pFilePath, 0);
            return 1;
        }

        fs_file_writef(STDOUT, "Created ticket %lu.\n", newID);
        return 0;
    }
}

static int create_ticket(const char* pMessage)
{
    static const char ticketPrefix[] = "status: open\n\n---\n\n";
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;

    if (pMessage != NULL) {
        size_t prefixLength = sizeof(ticketPrefix) - 1;
        size_t messageLength = strlen(pMessage);

        if (messageLength == 0) {
            fs_file_writef(STDERR, "A ticket message must not be empty.\n");
            return 1;
        }

        fileDataSize = prefixLength + messageLength + 1;
        pFileData = (char*)malloc(fileDataSize);
        if (pFileData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate ticket data.\n");
            return 1;
        }

        memcpy(pFileData, ticketPrefix, prefixLength);
        memcpy(pFileData + prefixLength, pMessage, messageLength);
        pFileData[fileDataSize - 1] = '\n';
    } else {
        if (!edit_text(ticketPrefix, sizeof(ticketPrefix) - 1, &pFileData, &fileDataSize)) {
            return 1;
        }
    }

    if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
        fs_file_writef(STDERR, "A new ticket must have a status and short description.\n");
        return 1;
    }

    if (!text_range_equal(pFileData, parsedTicket.status, "open")) {
        fs_file_writef(STDERR, "A new ticket must have an open status.\n");
        return 1;
    }

    return create_ticket_file(pFileData, fileDataSize);
}

static int list_tickets(const char* pStatus)
{
    fs_iterator* pIterator;

    for (pIterator = fs_first(NULL, g_pTicketsFolder, 0); pIterator != NULL; pIterator = fs_next(pIterator)) {
        const char* pID = "[Unknown ID]";
        size_t idLength = strlen(pID);
        char* pFileData;
        size_t fileDataSize;
        ticket parsedTicket;

        /* For now, just use the file name for the ID, but maybe later we can parse the file name to just take the first part which we assume is the ID. */
        pID = pIterator->pName;
        idLength = pIterator->nameLen;

        /* Now we need to open the file and parse the short description. */
        {
            fs_result result;
            char* pFilePath;

            pFilePath = get_ticket_path(pIterator->pName, pIterator->nameLen);
            if (pFilePath == NULL) {
                fs_file_writef(STDERR, "Failed to construct ticket path.\n");
                continue;
            }

            result = read_text_file(pFilePath, &pFileData, &fileDataSize);
            if (result != FS_SUCCESS) {
                fs_file_writef(STDERR, "Failed to read %s. %s.\n", pFilePath, fs_result_description(result));
                continue;
            }

            if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
                fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
                continue;
            }
        }

        if (pStatus != NULL && !text_range_equal(pFileData, parsedTicket.status, pStatus)) {
            continue;
        }

        fs_file_writef(STDOUT, "%.*s [%.*s] %.*s\n",
            (int)idLength, pID,
            (int)parsedTicket.status.length, pFileData + parsedTicket.status.offset,
            (int)parsedTicket.shortDescription.length, pFileData + parsedTicket.shortDescription.offset);
    }

    return 0;
}

static int is_ticket_id(const char* id)
{
    const char* pCharacter = id;

    if (pCharacter[0] == '\0') {
        return 0;
    }

    while (pCharacter[0] != '\0') {
        if (pCharacter[0] < '0' || pCharacter[0] > '9') {
            return 0;
        }

        pCharacter += 1;
    }

    return 1;
}

static int show_ticket(const char* id)
{
    fs_result result;
    fs_file* pFile;
    char* pFilePath;
    char buffer[4096];
    size_t bytesRead;

    if (!is_ticket_id(id)) {
        fs_file_writef(STDERR, "Invalid ticket ID: %s.\n", id);
        return 1;
    }

    pFilePath = get_ticket_path(id, FS_NULL_TERMINATED);
    if (pFilePath == NULL) {
        fs_file_writef(STDERR, "Failed to construct ticket path.\n");
        return 1;
    }

    result = fs_file_open(NULL, pFilePath, FS_READ, &pFile);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to open %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    for (;;) {
        result = fs_file_read(pFile, buffer, sizeof(buffer), &bytesRead);
        if (result == FS_AT_END) {
            break;
        }

        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to read %s. %s.\n", pFilePath, fs_result_description(result));
            fs_file_close(pFile);
            return 1;
        }

        result = fs_file_write(STDOUT, buffer, bytesRead, NULL);
        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to write ticket. %s.\n", fs_result_description(result));
            fs_file_close(pFile);
            return 1;
        }
    }

    fs_file_close(pFile);

    return 0;
}

static int edit_ticket(const char* id)
{
    fs_result result;
    fs_file* pFile;
    char* pFilePath;

    if (!is_ticket_id(id)) {
        fs_file_writef(STDERR, "Invalid ticket ID: %s.\n", id);
        return 1;
    }

    pFilePath = get_ticket_path(id, FS_NULL_TERMINATED);
    if (pFilePath == NULL) {
        fs_file_writef(STDERR, "Failed to construct ticket path.\n");
        return 1;
    }

    /* This is just checking the file exists and is readable. Is there a cleaner way of doing this? */
    {
        result = fs_file_open(NULL, pFilePath, FS_READ, &pFile);
        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to open %s. %s.\n", pFilePath, fs_result_description(result));
            return 1;
        }
        fs_file_close(pFile);
    }

    if (!run_text_editor(pFilePath)) {
        fs_file_writef(STDERR, "Text editor failed.\n");
        return 1;
    }

    return 0;
}

static int has_comment_body(const char* pText, size_t textLength)
{
    size_t cursor = 0;

    while (cursor < textLength && pText[cursor] != '\n') {
        cursor += 1;
    }

    while (cursor < textLength) {
        char character = pText[cursor];

        if (character != '\n' && character != '\r' && character != ' ' && character != '\t') {
            return 1;
        }

        cursor += 1;
    }

    return 0;
}

static int has_comment_author(const char* pText, size_t textLength)
{
    text_range line;
    text_range author;
    size_t lineLength = 0;
    size_t separatorOffset = 0;

    while (lineLength < textLength && pText[lineLength] != '\n') {
        lineLength += 1;
    }

    line = trim_line(pText, 0, lineLength);
    while (separatorOffset + 3 <= line.length) {
        if (memcmp(pText + line.offset + separatorOffset, " - ", 3) == 0) {
            author = trim_line(pText, line.offset + separatorOffset + 3, line.length - separatorOffset - 3);
            return author.length > 0 && !text_range_equal(pText, author, "<Insert Name>");
        }

        separatorOffset += 1;
    }

    return 0;
}

static int comment_on_ticket(const char* id)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;
    char* pCommentHeader;
    size_t commentHeaderLength;
    int authorFound;
    const char* pSeparator;
    size_t separatorLength;
    size_t initialDataSize;
    char* pInitialData;
    char* pWriteCursor;
    char* pEditedData;
    size_t editedDataSize;
    char* pUpdatedData;
    size_t updatedDataSize;
    size_t finalNewlineLength;

    if (!is_ticket_id(id)) {
        fs_file_writef(STDERR, "Invalid ticket ID: %s.\n", id);
        return 1;
    }

    pFilePath = get_ticket_path(id, FS_NULL_TERMINATED);
    if (pFilePath == NULL) {
        fs_file_writef(STDERR, "Failed to construct ticket path.\n");
        return 1;
    }

    result = read_text_file(pFilePath, &pFileData, &fileDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to read %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
        fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
        return 1;
    }

    pCommentHeader = create_comment_header(&commentHeaderLength, &authorFound);
    if (pCommentHeader == NULL) {
        return 1;
    }

    if (!authorFound) {
        fs_file_writef(STDERR, "No author name was found. Update <Insert Name> before saving.\n");
    }

    initialDataSize = commentHeaderLength + 2;
    pInitialData = (char*)malloc(initialDataSize);
    if (pInitialData == NULL) {
        fs_file_writef(STDERR, "Failed to allocate comment data.\n");
        return 1;
    }

    pWriteCursor = pInitialData;
    memcpy(pWriteCursor, pCommentHeader, commentHeaderLength);
    pWriteCursor += commentHeaderLength;
    memcpy(pWriteCursor, "\n\n", 2);

    if (!edit_text(pInitialData, initialDataSize, &pEditedData, &editedDataSize)) {
        return 1;
    }

    if (!has_comment_body(pEditedData, editedDataSize)) {
        fs_file_writef(STDERR, "A comment was not entered. Ticket was not changed.\n");
        return 1;
    }

    if (fileDataSize > 0 && pFileData[fileDataSize - 1] == '\n') {
        pSeparator = "\n---\n\n";
    } else {
        pSeparator = "\n\n---\n\n";
    }

    separatorLength = strlen(pSeparator);
    finalNewlineLength = editedDataSize > 0 && pEditedData[editedDataSize - 1] == '\n' ? 0 : 1;
    updatedDataSize = fileDataSize + separatorLength + editedDataSize + finalNewlineLength;
    pUpdatedData = (char*)malloc(updatedDataSize);
    if (pUpdatedData == NULL) {
        fs_file_writef(STDERR, "Failed to allocate comment data.\n");
        return 1;
    }

    pWriteCursor = pUpdatedData;
    memcpy(pWriteCursor, pFileData, fileDataSize);
    pWriteCursor += fileDataSize;
    memcpy(pWriteCursor, pSeparator, separatorLength);
    pWriteCursor += separatorLength;
    memcpy(pWriteCursor, pEditedData, editedDataSize);
    pWriteCursor += editedDataSize;

    if (finalNewlineLength > 0) {
        pWriteCursor[0] = '\n';
    }

    result = write_text_file(pFilePath, pUpdatedData, updatedDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    if (!has_comment_author(pEditedData, editedDataSize)) {
        fs_file_writef(STDERR, "Warning: Comment was saved without an author.\n");
    }

    return 0;
}

static int update_ticket_status(const char* id, const char* pStatus, int addComment)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;
    char* pUpdatedData;
    char* pFinalData;
    char* pWriteCursor;
    size_t statusLength;
    size_t updatedDataSize;
    size_t finalDataSize;
    const char* pSeparator;
    size_t separatorLength;
    const char* pMessagePrefix = "Changed status to ";
    size_t messagePrefixLength;
    char* pCommentHeader;
    size_t commentHeaderLength;
    int authorFound;

    if (!is_ticket_id(id)) {
        fs_file_writef(STDERR, "Invalid ticket ID: %s.\n", id);
        return 1;
    }

    pFilePath = get_ticket_path(id, FS_NULL_TERMINATED);
    if (pFilePath == NULL) {
        fs_file_writef(STDERR, "Failed to construct ticket path.\n");
        return 1;
    }

    result = read_text_file(pFilePath, &pFileData, &fileDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to read %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
        fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
        return 1;
    }

    if (text_range_equal(pFileData, parsedTicket.status, pStatus)) {
        return 0;   /* Status unchanged. */
    }

    statusLength = strlen(pStatus);
    pUpdatedData = replace_text_range(pFileData, fileDataSize, parsedTicket.status, pStatus, statusLength, &updatedDataSize);
    if (pUpdatedData == NULL) {
        fs_file_writef(STDERR, "Failed to allocate updated ticket data.\n");
        return 1;
    }

    pFinalData = pUpdatedData;
    finalDataSize = updatedDataSize;

    if (addComment) {
        pCommentHeader = create_comment_header(&commentHeaderLength, &authorFound);
        if (pCommentHeader == NULL) {
            return 1;
        }

        if (!authorFound) {
            fs_file_writef(STDERR, "Warning: No author name was found for status comment.\n");
        }

        if (updatedDataSize > 0 && pUpdatedData[updatedDataSize - 1] == '\n') {
            pSeparator = "\n---\n\n";
        } else {
            pSeparator = "\n\n---\n\n";
        }

        separatorLength = strlen(pSeparator);
        messagePrefixLength = strlen(pMessagePrefix);
        finalDataSize = updatedDataSize + separatorLength + commentHeaderLength + 2 + messagePrefixLength + statusLength + 2;
        pFinalData = (char*)malloc(finalDataSize);
        if (pFinalData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate status comment data.\n");
            return 1;
        }

        pWriteCursor = pFinalData;
        memcpy(pWriteCursor, pUpdatedData, updatedDataSize);
        pWriteCursor += updatedDataSize;
        memcpy(pWriteCursor, pSeparator, separatorLength);
        pWriteCursor += separatorLength;
        memcpy(pWriteCursor, pCommentHeader, commentHeaderLength);
        pWriteCursor += commentHeaderLength;
        memcpy(pWriteCursor, "\n\n", 2);
        pWriteCursor += 2;
        memcpy(pWriteCursor, pMessagePrefix, messagePrefixLength);
        pWriteCursor += messagePrefixLength;
        memcpy(pWriteCursor, pStatus, statusLength);
        pWriteCursor += statusLength;
        memcpy(pWriteCursor, ".\n", 2);
    }

    result = write_text_file(pFilePath, pFinalData, finalDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    return 0;
}


/* BEG testing */
static char* get_test_path(const char* pCaseName, const char* pFileName)
{
    int casePathLength;
    int pathLength;
    char* pCasePath;
    char* pPath;

    casePathLength = fs_path_append(NULL, 0, "tests/cases", FS_NULL_TERMINATED, pCaseName, FS_NULL_TERMINATED);
    if (casePathLength < 0) {
        return NULL;
    }

    pCasePath = (char*)malloc((size_t)casePathLength + 1);
    if (pCasePath == NULL) {
        return NULL;
    }
    fs_path_append(pCasePath, (size_t)casePathLength + 1, "tests/cases", FS_NULL_TERMINATED, pCaseName, FS_NULL_TERMINATED);

    pathLength = fs_path_append(NULL, 0, pCasePath, FS_NULL_TERMINATED, pFileName, FS_NULL_TERMINATED);
    pPath = (char*)malloc((size_t)pathLength + 1);
    if (pPath == NULL) {
        return NULL;
    }
    fs_path_append(pPath, (size_t)pathLength + 1, pCasePath, FS_NULL_TERMINATED, pFileName, FS_NULL_TERMINATED);
    return pPath;
}

static int test_case_name_compare(const void* pA, const void* pB)
{
    const char* const* ppA = (const char* const*)pA;
    const char* const* ppB = (const char* const*)pB;
    return strcmp(*ppA, *ppB);
}

static int test_case_matches(const char* pCaseName, const char* pFilter)
{
    if (pFilter == NULL) {
        return 1;
    }
    if (strlen(pFilter) == 3) {
        return strncmp(pCaseName, pFilter, 3) == 0 && pCaseName[3] == '_';
    }
    return strcmp(pCaseName, pFilter) == 0;
}

static int run_parser_test(const char* pCaseName)
{
    char* pScriptPath = get_test_path(pCaseName, "ticket");
    char* pExpectationPath = get_test_path(pCaseName, "expectation.txt");
    char* pExpectedPath = get_test_path(pCaseName, "expected.txt");
    char* pScript;
    char* pExpectation;
    char* pExpected;
    size_t scriptSize;
    size_t expectationSize;
    size_t expectedSize;
    ticket parsedTicket;
    int parseResult;

    if (pScriptPath == NULL || pExpectationPath == NULL ||
        read_text_file(pScriptPath, &pScript, &scriptSize) != FS_SUCCESS ||
        read_text_file(pExpectationPath, &pExpectation, &expectationSize) != FS_SUCCESS) {
        return 0;
    }

    while (expectationSize > 0 && (pExpectation[expectationSize - 1] == '\n' || pExpectation[expectationSize - 1] == '\r')) {
        expectationSize -= 1;
    }
    pExpectation[expectationSize] = '\0';
    parseResult = parse_ticket(pScript, scriptSize, &parsedTicket);

    if (strcmp(pExpectation, "parse_error") == 0) {
        return !parseResult;
    }
    if (strcmp(pExpectation, "parse_success") != 0 || !parseResult || pExpectedPath == NULL ||
        read_text_file(pExpectedPath, &pExpected, &expectedSize) != FS_SUCCESS) {
        return 0;
    }

    if (expectedSize != parsedTicket.status.length + parsedTicket.shortDescription.length + 2) {
        return 0;
    }

    return memcmp(pExpected, pScript + parsedTicket.status.offset, parsedTicket.status.length) == 0 &&
        pExpected[parsedTicket.status.length] == '\n' &&
        memcmp(pExpected + parsedTicket.status.length + 1,
            pScript + parsedTicket.shortDescription.offset, parsedTicket.shortDescription.length) == 0 &&
        pExpected[expectedSize - 1] == '\n';
}

static int run_tests(const char* pFilter)
{
    fs_iterator* pIterator;
    char** ppCaseNames = NULL;
    size_t caseCount = 0;
    size_t selectedCount = 0;
    size_t passedCount = 0;
    size_t i;

    if (pFilter != NULL && strlen(pFilter) != 3) {
        int numeric = 1;

        for (i = 0; pFilter[i] != '\0'; i += 1) {
            if (pFilter[i] < '0' || pFilter[i] > '9') {
                numeric = 0;
                break;
            }
        }
        if (numeric) {
            fs_file_writef(STDERR, "Numeric test filters must contain exactly three digits.\n");
            return 1;
        }
    }

    for (pIterator = fs_first(NULL, "tests/cases", 0); pIterator != NULL; pIterator = fs_next(pIterator)) {
        char** ppNewCaseNames;

        if (!pIterator->info.directory || pIterator->nameLen < 5 || pIterator->pName[3] != '_') {
            continue;
        }

        ppNewCaseNames = (char**)realloc(ppCaseNames, (caseCount + 1) * sizeof(*ppCaseNames));
        if (ppNewCaseNames == NULL) {
            fs_file_writef(STDERR, "Failed to allocate test data.\n");
            return 1;
        }
        ppCaseNames = ppNewCaseNames;
        ppCaseNames[caseCount] = (char*)malloc(pIterator->nameLen + 1);
        if (ppCaseNames[caseCount] == NULL) {
            fs_file_writef(STDERR, "Failed to allocate test data.\n");
            return 1;
        }
        memcpy(ppCaseNames[caseCount], pIterator->pName, pIterator->nameLen);
        ppCaseNames[caseCount][pIterator->nameLen] = '\0';
        caseCount += 1;
    }

    qsort(ppCaseNames, caseCount, sizeof(*ppCaseNames), test_case_name_compare);
    for (i = 0; i < caseCount; i += 1) {
        int passed;

        if (!test_case_matches(ppCaseNames[i], pFilter)) {
            continue;
        }

        selectedCount += 1;
        passed = run_parser_test(ppCaseNames[i]);
        if (passed) {
            passedCount += 1;
        }
        fs_file_writef(STDOUT, "%s: %s\n", passed ? "PASS" : "FAIL", ppCaseNames[i]);
    }

    if (caseCount == 0) {
        fs_file_writef(STDERR, "No test cases exist.\n");
    } else if (selectedCount == 0) {
        fs_file_writef(STDERR, "Requested test case does not exist.\n");
    }
    fs_file_writef(STDOUT, "SUMMARY: %d/%d passed\n", (int)passedCount, (int)selectedCount);

    return selectedCount > 0 && passedCount == selectedCount ? 0 : 1;
}
/* END testing */


int main(int argc, char** argv)
{
    int argumentIndex = 1;

    fs_file_open(NULL, FS_STDIN,  FS_READ,  &STDIN );
    fs_file_open(NULL, FS_STDOUT, FS_WRITE, &STDOUT);
    fs_file_open(NULL, FS_STDERR, FS_WRITE, &STDERR);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[argumentIndex], "--tickets-folder") == 0 || strcmp(argv[argumentIndex], "-t") == 0) {
        if (argc <= argumentIndex + 1 || argv[argumentIndex + 1][0] == '\0') {
            fs_file_writef(STDERR, "The %s option requires a path.\n", argv[argumentIndex]);
            print_usage(argv[0]);
            return 1;
        }

        g_pTicketsFolder = argv[argumentIndex + 1];
        argumentIndex += 2;

        if (argc <= argumentIndex) {
            fs_file_writef(STDERR, "A command is required.\n");
            print_usage(argv[0]);
            return 1;
        }
    }

    if (strcmp(argv[argumentIndex], "list") == 0) {
        const char* pStatus = NULL;

        if (argc > argumentIndex + 1) {
            if (strcmp(argv[argumentIndex + 1], "--status") != 0) {
                fs_file_writef(STDERR, "Unknown list option: %s.\n", argv[argumentIndex + 1]);
                print_usage(argv[0]);
                return 1;
            }

            if (argc <= argumentIndex + 2) {
                fs_file_writef(STDERR, "The --status option requires a value.\n");
                print_usage(argv[0]);
                return 1;
            }

            if (argc != argumentIndex + 3) {
                fs_file_writef(STDERR, "The list command has too many arguments.\n");
                print_usage(argv[0]);
                return 1;
            }

            pStatus = argv[argumentIndex + 2];
            if (strcmp(pStatus, "open") != 0 && strcmp(pStatus, "closed") != 0) {
                fs_file_writef(STDERR, "Invalid status: %s. Expected open or closed.\n", pStatus);
                return 1;
            }
        }

        return list_tickets(pStatus);
    }

    if (strcmp(argv[argumentIndex], "show") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The show command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return show_ticket(argv[argumentIndex + 1]);
    }

    if (strcmp(argv[argumentIndex], "edit") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The edit command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return edit_ticket(argv[argumentIndex + 1]);
    }

    if (strcmp(argv[argumentIndex], "close") == 0 || strcmp(argv[argumentIndex], "reopen") == 0) {
        const char* pStatus;
        int addComment = 1;

        if (argc < argumentIndex + 2 || argc > argumentIndex + 3) {
            fs_file_writef(STDERR, "The %s command requires one ticket ID and an optional --no-comment option.\n", argv[argumentIndex]);
            print_usage(argv[0]);
            return 1;
        }

        if (argc == argumentIndex + 3) {
            if (strcmp(argv[argumentIndex + 2], "--no-comment") != 0) {
                fs_file_writef(STDERR, "Unknown %s option: %s.\n", argv[argumentIndex], argv[argumentIndex + 2]);
                print_usage(argv[0]);
                return 1;
            }
            
            addComment = 0;
        }

        if (strcmp(argv[argumentIndex], "close") == 0) {
            pStatus = "closed";
        } else {
            pStatus = "open";
        }

        return update_ticket_status(argv[argumentIndex + 1], pStatus, addComment);
    }

    if (strcmp(argv[argumentIndex], "new") == 0) {
        const char* pMessage = NULL;

        if (argc > argumentIndex + 1) {
            if (strcmp(argv[argumentIndex + 1], "-m") != 0 && strcmp(argv[argumentIndex + 1], "--message") != 0) {
                fs_file_writef(STDERR, "Unknown new option: %s.\n", argv[argumentIndex + 1]);
                print_usage(argv[0]);
                return 1;
            }

            if (argc <= argumentIndex + 2) {
                fs_file_writef(STDERR, "The %s option requires a message.\n", argv[argumentIndex + 1]);
                print_usage(argv[0]);
                return 1;
            }

            if (argc != argumentIndex + 3) {
                fs_file_writef(STDERR, "The new command has too many arguments.\n");
                print_usage(argv[0]);
                return 1;
            }

            pMessage = argv[argumentIndex + 2];
        }

        return create_ticket(pMessage);
    }

    if (strcmp(argv[argumentIndex], "comment") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The comment command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return comment_on_ticket(argv[argumentIndex + 1]);
    }

    if (strcmp(argv[argumentIndex], "--test") == 0) {
        if (argc > argumentIndex + 2) {
            fs_file_writef(STDERR, "The --test option takes no more than one case.\n");
            return 1;
        }

        return run_tests(argc == argumentIndex + 2 ? argv[argumentIndex + 1] : NULL);
    }

    if (strcmp(argv[argumentIndex], "--help") == 0 || strcmp(argv[argumentIndex], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    fs_file_writef(STDERR, "Unknown command: %s\n", argv[argumentIndex]);
    print_usage(argv[0]);

    return 1;
}
