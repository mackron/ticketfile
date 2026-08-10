#include <limits.h>
#include <stdlib.h>

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
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] close <id>\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] reopen <id>\n", executablePath);
    fs_file_writef(STDOUT, "  %s [-t <path> | --tickets-folder <path>] new [-m <message> | --message <message>]\n", executablePath);
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

    return pTicket->status.length > 0 && pTicket->shortDescription.length > 0;
}

static int text_range_equal(const char* pText, text_range range, const char* pValue)
{
    size_t valueLength = strlen(pValue);

    return range.length == valueLength && memcmp(pText + range.offset, pValue, valueLength) == 0;
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
    fs_result result;
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
        char temporaryPath[1024];

        result = fs_mktmp("ticket", temporaryPath, sizeof(temporaryPath), FS_MKTMP_FILE);
        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to create a temporary ticket. %s.\n", fs_result_description(result));
            return 1;
        }

        result = write_text_file(temporaryPath, ticketPrefix, sizeof(ticketPrefix) - 1);
        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to write temporary ticket. %s.\n", fs_result_description(result));
            fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
            return 1;
        }

        if (!run_text_editor(temporaryPath)) {
            fs_file_writef(STDERR, "Text editor failed.\n");
            fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
            return 1;
        }

        result = read_text_file(temporaryPath, &pFileData, &fileDataSize);
        fs_remove(NULL, temporaryPath, FS_IGNORE_MOUNTS);
        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to read temporary ticket. %s.\n", fs_result_description(result));
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

static int update_ticket_status(const char* id, const char* pStatus)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;
    char* pUpdatedData;
    size_t statusLength;
    size_t updatedDataSize;

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

    result = write_text_file(pFilePath, pUpdatedData, updatedDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    return 0;
}

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

    if (strcmp(argv[argumentIndex], "close") == 0 || strcmp(argv[argumentIndex], "reopen") == 0) {
        const char* pStatus;

        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The %s command requires one ticket ID.\n", argv[argumentIndex]);
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argv[argumentIndex], "close") == 0) {
            pStatus = "closed";
        } else {
            pStatus = "open";
        }

        return update_ticket_status(argv[argumentIndex + 1], pStatus);
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

    if (strcmp(argv[argumentIndex], "--help") == 0 || strcmp(argv[argumentIndex], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    fs_file_writef(STDERR, "Unknown command: %s\n", argv[argumentIndex]);
    print_usage(argv[0]);

    return 1;
}
