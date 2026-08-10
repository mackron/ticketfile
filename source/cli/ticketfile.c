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
}


/* BEG parser */
typedef struct
{
    size_t offset;
    size_t length;
} ticket_text_range;

typedef struct
{
    ticket_text_range status;
    ticket_text_range shortDescription;
} ticket;

static int is_horizontal_whitespace(char character)
{
    return character == ' ' || character == '\t' || character == '\r';
}

static ticket_text_range trim_line(const char* pText, size_t lineOffset, size_t lineLength)
{
    ticket_text_range range;

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
        ticket_text_range line;
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
            ticket_text_range key;
            ticket_text_range value;

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
        ticket_text_range line;
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

static int ticket_text_range_equal(const char* pText, ticket_text_range range, const char* pValue)
{
    size_t valueLength = strlen(pValue);

    return range.length == valueLength && memcmp(pText + range.offset, pValue, valueLength) == 0;
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
            fs_file* pFile;
            char* pFilePath;

            pFilePath = get_ticket_path(pIterator->pName, pIterator->nameLen);
            if (pFilePath == NULL) {
                fs_file_writef(STDERR, "Failed to construct ticket path.\n");
                continue;
            }

            result = fs_file_open(NULL, pFilePath, FS_READ, &pFile);
            if (result != FS_SUCCESS) {
                fs_file_writef(STDERR, "Failed to open %s. %s.\n", pFilePath, fs_result_description(result));
                continue;
            }

            result = fs_file_read_to_end(pFile, FS_FORMAT_TEXT, (void**)&pFileData, &fileDataSize);
            fs_file_close(pFile);

            if (result != FS_SUCCESS) {
                fs_file_writef(STDERR, "Failed to read %s. %s.\n", pFilePath, fs_result_description(result));
                continue;
            }

            if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
                fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
                continue;
            }
        }

        if (pStatus != NULL && !ticket_text_range_equal(pFileData, parsedTicket.status, pStatus)) {
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

    if (strcmp(argv[argumentIndex], "--help") == 0 || strcmp(argv[argumentIndex], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    fs_file_writef(STDERR, "Unknown command: %s\n", argv[argumentIndex]);
    print_usage(argv[0]);

    return 1;
}
