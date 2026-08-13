#include <stdio.h>
#include <time.h>

#include "../../external/fs/fs.c"
#include "../ticketfile_version.h"

static fs_file* STDIN  = NULL;
static fs_file* STDOUT = NULL;
static fs_file* STDERR = NULL;

static const char* g_pTicketsFolder = "tickets";

static char* get_environment_variable(const char* pName)
{
    char* pValue;
    size_t valueLength;

    #if defined(_MSC_VER)
    {
        valueLength = 0;
        if (getenv_s(&valueLength, NULL, 0, pName) != 0 || valueLength == 0) {
            return NULL;
        }

        pValue = (char*)malloc(valueLength);
        if (pValue == NULL) {
            return NULL;
        }

        if (getenv_s(&valueLength, pValue, valueLength, pName) != 0) {
            free(pValue);
            return NULL;
        }
    }
    #else
    {
        const char* pEnvironmentValue = getenv(pName);

        if (pEnvironmentValue == NULL) {
            return NULL;
        }

        valueLength = strlen(pEnvironmentValue) + 1;
        pValue = (char*)malloc(valueLength);
        if (pValue == NULL) {
            return NULL;
        }

        memcpy(pValue, pEnvironmentValue, valueLength);
    }
    #endif

    return pValue;
}

static void print_version(void)
{
    fs_file_writef(STDOUT, "ticket v%d.%d.%d\n", TICKETFILE_VERSION_MAJOR, TICKETFILE_VERSION_MINOR, TICKETFILE_VERSION_PATCH);
}

static void print_usage(const char* pExecutablePath)
{
    fs_file_writef(STDOUT, "USAGE:\n");
    fs_file_writef(STDOUT,"    %s [<global args>] <command> [<args>]\n", pExecutablePath);
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "OPTIONS:\n");
    fs_file_writef(STDOUT, "    -h, --help\n");
    fs_file_writef(STDOUT, "        Show this help text.\n");
    fs_file_writef(STDOUT, "    -v, --version\n");
    fs_file_writef(STDOUT, "        Show version information.\n");
    fs_file_writef(STDOUT, "    -d <path>, --directory <path>\n");
    fs_file_writef(STDOUT, "        Use <path> as the tickets directory. Default: tickets.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "COMMANDS:\n");
    fs_file_writef(STDOUT, "    list [<tag:value> ...]\n");
    fs_file_writef(STDOUT, "        List tickets that match all valid metadata filters.\n");
    fs_file_writef(STDOUT, "        Invalid filters are ignored. With no filters, list all.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    show <id>\n");
    fs_file_writef(STDOUT, "        Write the complete ticket file to standard output.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    description <id> [--short | --detailed]\n");
    fs_file_writef(STDOUT, "        Write all or part of the ticket description.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    edit <id>\n");
    fs_file_writef(STDOUT, "        Open the ticket in VISUAL, EDITOR, or the default editor.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    get <id> <key>\n");
    fs_file_writef(STDOUT, "        Write one metadata value to standard output.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    set <id> <key:value> [<key:value> ...] [<comment options>]\n");
    fs_file_writef(STDOUT, "        Set one or more metadata values.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    clear <id> <key> [<key> ...] [<comment options>]\n");
    fs_file_writef(STDOUT, "        Remove one or more metadata values.\n");
    fs_file_writef(STDOUT, "        For set and clear, comment options are -m <text>, --message <text>,\n");
    fs_file_writef(STDOUT, "        -F <path>, --file <path>, and --no-comment.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    new [-m <text> | --message <text> |\n");
    fs_file_writef(STDOUT, "         -F <path> | --file <path>]\n");
    fs_file_writef(STDOUT, "        Create an open ticket. Without input, open an editor.\n");
    fs_file_writef(STDOUT, "        Use -m for inline text or -F to read text from a file.\n");
    fs_file_writef(STDOUT, "\n");
    fs_file_writef(STDOUT, "    comment <id>\n");
    fs_file_writef(STDOUT, "        Open an editor and append a dated comment to the ticket.\n");
}


/* BEG parser */
typedef struct
{
    size_t offset;
    size_t length;
} text_range;

typedef struct
{
    text_range key;
    text_range value;
} ticket_metadata;

typedef struct
{
    text_range status;
    text_range description;
    text_range shortDescription;
    text_range detailedDescription;
    ticket_metadata* pMetadata;
    size_t metadataCount;
    int hasMetadataSection;
    int statusFound;
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

static int text_range_equal(const char* pTextA, text_range rangeA, const char* pTextB, text_range rangeB)
{
    return rangeA.length == rangeB.length &&
        memcmp(pTextA + rangeA.offset, pTextB + rangeB.offset, rangeA.length) == 0;
}

static int text_range_equal_string(const char* pText, text_range range, const char* pValue)
{
    text_range valueRange;

    valueRange.offset = 0;
    valueRange.length = strlen(pValue);

    return text_range_equal(pText, range, pValue, valueRange);
}

static int parse_metadata_key_only_argument(const char* pArgument, text_range* pKey)
{
    *pKey = trim_line(pArgument, 0, strlen(pArgument));

    return pKey->length > 0 && memchr(pArgument + pKey->offset, ':', pKey->length) == NULL;
}

static int parse_metadata_set_argument(const char* pArgument, text_range* pKey, text_range* pValue)
{
    const char* pSeparator = strchr(pArgument, ':');
    size_t argumentLength = strlen(pArgument);
    size_t separatorOffset;

    if (pSeparator == NULL) {
        return 0;
    }

    separatorOffset = (size_t)(pSeparator - pArgument);
    *pKey   = trim_line(pArgument, 0, separatorOffset);
    *pValue = trim_line(pArgument, separatorOffset + 1, argumentLength - separatorOffset - 1);

    return pKey->length > 0 && pValue->length > 0;
}

static int metadata_arguments_have_duplicate_keys(int argumentCount, char** ppArguments, int hasValues)
{
    int argumentIndexA;

    for (argumentIndexA = 0; argumentIndexA < argumentCount; ++argumentIndexA) {
        text_range keyA;
        text_range valueA;
        int argumentIndexB;

        if (hasValues) {
            parse_metadata_set_argument(ppArguments[argumentIndexA], &keyA, &valueA);
        } else {
            parse_metadata_key_only_argument(ppArguments[argumentIndexA], &keyA);
        }

        for (argumentIndexB = argumentIndexA + 1; argumentIndexB < argumentCount; ++argumentIndexB) {
            text_range keyB;
            text_range valueB;

            if (hasValues) {
                parse_metadata_set_argument(ppArguments[argumentIndexB], &keyB, &valueB);
            } else {
                parse_metadata_key_only_argument(ppArguments[argumentIndexB], &keyB);
            }

            if (text_range_equal(ppArguments[argumentIndexA], keyA, ppArguments[argumentIndexB], keyB)) {
                return 1;
            }
        }
    }

    return 0;
}

static int validate_metadata_arguments(const char* pCommand, int argumentCount, char** ppArguments)
{
    int hasValues = strcmp(pCommand, "set") == 0;
    int argumentIndex;

    for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex += 1) {
        const char* pArgument = ppArguments[argumentIndex];
        text_range key;
        text_range value;
        int isValid;

        if (strcmp(pArgument, "--no-comment") == 0) {
            fs_file_writef(STDERR, "The --no-comment option must be the final argument.\n");
            return 0;
        }

        isValid = hasValues ? parse_metadata_set_argument(pArgument, &key, &value) : parse_metadata_key_only_argument(pArgument, &key);
        if (!isValid) {
            fs_file_writef(STDERR, "Invalid %s metadata argument: %s.\n", pCommand, pArgument);
            return 0;
        }
    }

    if (metadata_arguments_have_duplicate_keys(argumentCount, ppArguments, hasValues)) {
        fs_file_writef(STDERR, "The %s command contains a duplicate metadata key.\n", pCommand);
        return 0;
    }

    return 1;
}

typedef struct
{
    int metadataArgumentCount;
    int addGeneratedComment;
    const char* pMessage;
    size_t messageLength;
} metadata_command_options;

static fs_result read_text_file(const char* pFilePath, char** ppFileData, size_t* pFileDataSize);

static int text_has_non_whitespace(const char* pText, size_t textLength)
{
    size_t i;

    for (i = 0; i < textLength; i += 1) {
        if (pText[i] != ' ' && pText[i] != '\t' && pText[i] != '\r' && pText[i] != '\n') {
            return 1;
        }
    }

    return 0;
}

static int parse_metadata_command_options(int argumentCount, char** ppArguments, metadata_command_options* pOptions)
{
    int argumentIndex;
    int messageFound = 0;

    pOptions->metadataArgumentCount = argumentCount;
    pOptions->addGeneratedComment = 1;
    pOptions->pMessage = NULL;
    pOptions->messageLength = 0;

    for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex += 1) {
        const char* pArgument = ppArguments[argumentIndex];
        int isMessage = strcmp(pArgument, "-m") == 0 || strcmp(pArgument, "--message") == 0;
        int isFile = strcmp(pArgument, "-F") == 0 || strcmp(pArgument, "--file") == 0;

        if (pArgument[0] != '-') {
            if (pOptions->metadataArgumentCount != argumentCount) {
                fs_file_writef(STDERR, "Metadata arguments must come before comment options.\n");
                return 0;
            }
            continue;
        }

        if (pOptions->metadataArgumentCount == argumentCount) {
            pOptions->metadataArgumentCount = argumentIndex;
        }

        if (strcmp(pArgument, "--no-comment") == 0) {
            if (!pOptions->addGeneratedComment) {
                fs_file_writef(STDERR, "The --no-comment option must not occur more than once.\n");
                return 0;
            }
            pOptions->addGeneratedComment = 0;
            continue;
        }

        if (isMessage || isFile) {
            if (messageFound) {
                fs_file_writef(STDERR, "Message and file options are mutually exclusive and must not repeat.\n");
                return 0;
            }
            if (argumentIndex + 1 >= argumentCount) {
                fs_file_writef(STDERR, "The %s option requires a value.\n", pArgument);
                return 0;
            }

            argumentIndex += 1;
            if (isFile) {
                fs_result result = read_text_file(ppArguments[argumentIndex], (char**)&pOptions->pMessage, &pOptions->messageLength);

                if (result != FS_SUCCESS) {
                    fs_file_writef(STDERR, "Failed to read %s. %s.\n", ppArguments[argumentIndex], fs_result_description(result));
                    return 0;
                }
            } else {
                pOptions->pMessage = ppArguments[argumentIndex];
                pOptions->messageLength = strlen(pOptions->pMessage);
            }

            if (!text_has_non_whitespace(pOptions->pMessage, pOptions->messageLength)) {
                fs_file_writef(STDERR, "A metadata comment message must not be empty.\n");
                return 0;
            }
            messageFound = 1;
            continue;
        }

        fs_file_writef(STDERR, "Unknown metadata comment option: %s.\n", pArgument);
        return 0;
    }

    return 1;
}

static int parse_ticket(const char* pText, size_t textLength, ticket* pTicket)
{
    size_t cursor = 0;
    size_t descriptionEnd = textLength;
    int foundSeparator = 0;
    int validMetadataSection = 1;

    pTicket->status.offset = 0;
    pTicket->status.length = 0;
    pTicket->description.offset = 0;
    pTicket->description.length = 0;
    pTicket->shortDescription.offset = 0;
    pTicket->shortDescription.length = 0;
    pTicket->detailedDescription.offset = 0;
    pTicket->detailedDescription.length = 0;
    pTicket->pMetadata = NULL;
    pTicket->metadataCount = 0;
    pTicket->hasMetadataSection = 0;
    pTicket->statusFound = 0;

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

        if (line.length == 0) {
            continue;
        }

        if (colonOffset < line.length) {
            text_range key;
            text_range value;
            ticket_metadata* pNewMetadata;

            key   = trim_line(pText, line.offset,                   colonOffset);
            value = trim_line(pText, line.offset + colonOffset + 1, line.length - colonOffset - 1);

            if (key.length == 0) {
                validMetadataSection = 0;
                continue;
            }

            pNewMetadata = (ticket_metadata*)realloc(pTicket->pMetadata, (pTicket->metadataCount + 1) * sizeof(*pNewMetadata));
            if (pNewMetadata == NULL) {
                return 0;
            }

            pTicket->pMetadata = pNewMetadata;
            pTicket->pMetadata[pTicket->metadataCount].key = key;
            pTicket->pMetadata[pTicket->metadataCount].value = value;
            pTicket->metadataCount += 1;

            if (key.length == 6 && memcmp(pText + key.offset, "status", 6) == 0) {
                pTicket->status = value;
                pTicket->statusFound = 1;
            }
        } else {
            validMetadataSection = 0;
        }
    }

    if (foundSeparator && validMetadataSection && pTicket->metadataCount > 0) {
        pTicket->hasMetadataSection = 1;
    } else {
        cursor = 0;
        pTicket->status.offset = 0;
        pTicket->status.length = 0;
        pTicket->metadataCount = 0;
        pTicket->statusFound = 0;
    }

    while (cursor < textLength) {
        text_range line;
        size_t lineOffset = cursor;
        size_t nextLineOffset;

        while (cursor < textLength && pText[cursor] != '\n') {
            cursor += 1;
        }

        line = trim_line(pText, lineOffset, cursor - lineOffset);
        if (cursor < textLength) {
            cursor += 1;
        }
        nextLineOffset = cursor;

        if (line.length == 3 && memcmp(pText + line.offset, "---", 3) == 0) {
            descriptionEnd = lineOffset;
            break;
        }

        if (line.length > 0 && pTicket->shortDescription.length == 0) {
            pTicket->shortDescription = line;
            pTicket->description.offset = line.offset;
            pTicket->detailedDescription.offset = nextLineOffset;
        }
    }

    if (pTicket->shortDescription.length > 0) {
        while (descriptionEnd > pTicket->description.offset && (pText[descriptionEnd - 1] == ' '  || pText[descriptionEnd - 1] == '\t' || pText[descriptionEnd - 1] == '\r' || pText[descriptionEnd - 1] == '\n')) {
            descriptionEnd -= 1;
        }

        pTicket->description.length = descriptionEnd - pTicket->description.offset;

        while (pTicket->detailedDescription.offset < descriptionEnd && (pText[pTicket->detailedDescription.offset] == ' ' || pText[pTicket->detailedDescription.offset] == '\t' || pText[pTicket->detailedDescription.offset] == '\r' || pText[pTicket->detailedDescription.offset] == '\n')) {
            pTicket->detailedDescription.offset += 1;
        }

        if (pTicket->detailedDescription.offset < descriptionEnd) {
            pTicket->detailedDescription.length = descriptionEnd - pTicket->detailedDescription.offset;
        } else {
            pTicket->detailedDescription.offset = descriptionEnd;
        }
    }

    return 1;
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

    if (replacementLength > 0) {
        memcpy(pWriteCursor, pReplacement, replacementLength);
        pWriteCursor += replacementLength;
    }

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

static fs_result replace_file(const char* pSourcePath, const char* pDestinationPath)
{
    #if defined(FS_WIN32)
    {
        fs_result result;
        fs_win32_path sourcePath;
        fs_win32_path destinationPath;

        result = fs_win32_path_init(&sourcePath, pSourcePath, FS_NULL_TERMINATED, NULL);
        if (result != FS_SUCCESS) {
            return result;
        }

        result = fs_win32_path_init(&destinationPath, pDestinationPath, FS_NULL_TERMINATED, NULL);
        if (result != FS_SUCCESS) {
            fs_win32_path_uninit(&sourcePath, NULL);
            return result;
        }

        if (!MoveFileEx(sourcePath.path, destinationPath.path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result = fs_result_from_GetLastError();
        }

        fs_win32_path_uninit(&sourcePath, NULL);
        fs_win32_path_uninit(&destinationPath, NULL);
        return result;
    }
    #else
    {
        if (rename(pSourcePath, pDestinationPath) != 0) {
            return fs_result_from_errno(errno);
        }

        return FS_SUCCESS;
    }
    #endif
}

static fs_result replace_text_file(const char* pFilePath, const void* pFileData, size_t fileDataSize)
{
    fs_result result;
    fs_file* pFile;
    char* pTemporaryPath;
    size_t temporaryPathCapacity;
    unsigned long suffix = 0;

    temporaryPathCapacity = strlen(pFilePath) + 3 * sizeof(unsigned long) + 18;
    pTemporaryPath = (char*)malloc(temporaryPathCapacity);
    if (pTemporaryPath == NULL) {
        return FS_OUT_OF_MEMORY;
    }

    for (;;) {
        fs_snprintf(pTemporaryPath, temporaryPathCapacity, "%s.ticketfile-tmp-%lu", pFilePath, suffix);
        result = fs_file_open(NULL, pTemporaryPath, FS_WRITE | FS_EXCLUSIVE, &pFile);
        if (result != FS_ALREADY_EXISTS) {
            break;
        }

        if (suffix == ULONG_MAX) {
            return FS_ALREADY_EXISTS;
        }

        suffix += 1;
    }

    if (result != FS_SUCCESS) {
        return result;
    }

    result = fs_file_write(pFile, pFileData, fileDataSize, NULL);
    fs_file_close(pFile);
    if (result != FS_SUCCESS) {
        fs_remove(NULL, pTemporaryPath, 0);
        return result;
    }

    result = replace_file(pTemporaryPath, pFilePath);
    if (result != FS_SUCCESS) {
        fs_remove(NULL, pTemporaryPath, 0);
    }

    return result;
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

    pEditor = get_environment_variable("VISUAL");
    if (pEditor == NULL || pEditor[0] == '\0') {
        pEditor = get_environment_variable("EDITOR");
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
    const char* pEnvironmentAuthor = get_environment_variable("TICKET_AUTHOR");

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
    struct tm localTime;

    *pAuthorFound = get_comment_author(author, sizeof(author));

    currentTime = time(NULL);
    #if defined(_MSC_VER)
    {
        if (localtime_s(&localTime, &currentTime) != 0) {
            fs_file_writef(STDERR, "Failed to determine current date.\n");
            return NULL;
        }
    }
    #else
    {
        struct tm* pLocalTime = localtime(&currentTime);

        if (pLocalTime == NULL) {
            fs_file_writef(STDERR, "Failed to determine current date.\n");
            return NULL;
        }

        localTime = *pLocalTime;
    }
    #endif

    if (strftime(date, sizeof(date), "%Y-%m-%d", &localTime) == 0) {
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

        if (pIterator->info.directory) {
            continue;
        }

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

static int create_ticket(const char* pMessage, const char* pDescriptionPath)
{
    static const char ticketTemplate[] = "status: open\n\n---\n\n";
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;

    if (pMessage != NULL || pDescriptionPath != NULL) {
        size_t templateLength = sizeof(ticketTemplate) - 1;
        char* pDescriptionData;
        const char* pDescription;
        size_t descriptionLength;
        size_t finalNewlineLength;

        if (pDescriptionPath != NULL) {
            fs_result result = read_text_file(pDescriptionPath, &pDescriptionData, &descriptionLength);

            if (result != FS_SUCCESS) {
                fs_file_writef(STDERR, "Failed to read %s. %s.\n", pDescriptionPath, fs_result_description(result));
                return 1;
            }

            pDescription = pDescriptionData;
            finalNewlineLength = 0;
        } else {
            pDescription = pMessage;
            descriptionLength = strlen(pMessage);
            finalNewlineLength = 1;
        }

        if (pMessage != NULL && descriptionLength == 0) {
            fs_file_writef(STDERR, "A ticket message must not be empty.\n");
            return 1;
        }

        fileDataSize = templateLength + descriptionLength + finalNewlineLength;
        pFileData = (char*)malloc(fileDataSize);
        if (pFileData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate ticket data.\n");
            return 1;
        }

        memcpy(pFileData, ticketTemplate, templateLength);
        if (descriptionLength > 0) {
            memcpy(pFileData + templateLength, pDescription, descriptionLength);
        }
        if (finalNewlineLength > 0) {
            pFileData[fileDataSize - 1] = '\n';
        }
    } else {
        if (!edit_text(ticketTemplate, sizeof(ticketTemplate) - 1, &pFileData, &fileDataSize)) {
            return 1;
        }
    }

    if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
        fs_file_writef(STDERR, "Failed to parse new ticket.\n");
        return 1;
    }

    return create_ticket_file(pFileData, fileDataSize);
}

static int ticket_matches_filter(const char* pText, const ticket* pTicket, const char* pFilter)
{
    text_range filter;
    text_range key;
    text_range value;
    size_t colonOffset = 0;
    size_t i;

    filter = trim_line(pFilter, 0, strlen(pFilter));
    while (colonOffset < filter.length && pFilter[filter.offset + colonOffset] != ':') {
        colonOffset += 1;
    }

    if (colonOffset == filter.length) {
        return 1;
    }

    key   = trim_line(pFilter, filter.offset,                   colonOffset);
    value = trim_line(pFilter, filter.offset + colonOffset + 1, filter.length - colonOffset - 1);
    if (key.length == 0 || value.length == 0) {
        return 1;
    }

    for (i = pTicket->metadataCount; i > 0; i -= 1) {
        const ticket_metadata* pMetadata = &pTicket->pMetadata[i - 1];

        if (text_range_equal(pText, pMetadata->key, pFilter, key)) {
            return text_range_equal(pText, pMetadata->value, pFilter, value);
        }
    }

    return 0;
}

static int ticket_matches_filters(const char* pText, const ticket* pTicket, int filterCount, char** ppFilters)
{
    int i;

    for (i = 0; i < filterCount; i += 1) {
        if (!ticket_matches_filter(pText, pTicket, ppFilters[i])) {
            return 0;
        }
    }

    return 1;
}

static int ticket_file_name_compare(const void* pA, const void* pB)
{
    const char* pNameA = *(const char* const*)pA;
    const char* pNameB = *(const char* const*)pB;
    unsigned long idA;
    unsigned long idB;
    int numericA = parse_ticket_id(pNameA, strlen(pNameA), &idA);
    int numericB = parse_ticket_id(pNameB, strlen(pNameB), &idB);

    if (numericA && numericB) {
        if (idA < idB) {
            return -1;
        }
        if (idA > idB) {
            return 1;
        }
    } else if (numericA) {
        return -1;
    } else if (numericB) {
        return 1;
    }

    return strcmp(pNameA, pNameB);
}

static int list_tickets(int filterCount, char** ppFilters)
{
    fs_iterator* pIterator;
    char** ppFileNames = NULL;
    size_t fileNameCount = 0;
    size_t i;

    for (pIterator = fs_first(NULL, g_pTicketsFolder, 0); pIterator != NULL; pIterator = fs_next(pIterator)) {
        char** ppNewFileNames;

        if (pIterator->info.directory || strstr(pIterator->pName, ".ticketfile-tmp-") != NULL) {
            continue;
        }

        ppNewFileNames = (char**)realloc(ppFileNames, (fileNameCount + 1) * sizeof(*ppFileNames));
        if (ppNewFileNames == NULL) {
            fs_file_writef(STDERR, "Failed to allocate ticket list.\n");
            return 1;
        }
        ppFileNames = ppNewFileNames;

        ppFileNames[fileNameCount] = (char*)malloc(pIterator->nameLen + 1);
        if (ppFileNames[fileNameCount] == NULL) {
            fs_file_writef(STDERR, "Failed to allocate ticket list.\n");
            return 1;
        }
        memcpy(ppFileNames[fileNameCount], pIterator->pName, pIterator->nameLen + 1);
        fileNameCount += 1;
    }

    if (fileNameCount > 1) {
        qsort(ppFileNames, fileNameCount, sizeof(*ppFileNames), ticket_file_name_compare);
    }

    for (i = 0; i < fileNameCount; i += 1) {
        const char* pID = ppFileNames[i];
        size_t idLength = strlen(pID);
        char* pFileData;
        size_t fileDataSize;
        ticket parsedTicket;

        /* Now we need to open the file and parse the short description. */
        {
            fs_result result;
            char* pFilePath;

            pFilePath = get_ticket_path(pID, idLength);
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

        if (!ticket_matches_filters(pFileData, &parsedTicket, filterCount, ppFilters)) {
            continue;
        }

        if (parsedTicket.statusFound && parsedTicket.status.length > 0) {
            fs_file_writef(STDOUT, "%.*s [%.*s] %.*s\n",
                (int)idLength, pID,
                (int)parsedTicket.status.length, pFileData + parsedTicket.status.offset,
                (int)parsedTicket.shortDescription.length, pFileData + parsedTicket.shortDescription.offset);
        } else {
            fs_file_writef(STDOUT, "%.*s %.*s\n",
                (int)idLength, pID,
                (int)parsedTicket.shortDescription.length, pFileData + parsedTicket.shortDescription.offset);
        }
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

static int show_ticket_description(const char* id, int descriptionPart)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;
    text_range range;

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

    range = parsedTicket.description;
    if (descriptionPart == 1) {
        range = parsedTicket.shortDescription;
    } else if (descriptionPart == 2) {
        range = parsedTicket.detailedDescription;
    }

    if (range.length > 0) {
        result = fs_file_write(STDOUT, pFileData + range.offset, range.length, NULL);
        if (result == FS_SUCCESS) {
            result = fs_file_write(STDOUT, "\n", 1, NULL);
        }

        if (result != FS_SUCCESS) {
            fs_file_writef(STDERR, "Failed to write ticket description. %s.\n", fs_result_description(result));
            return 1;
        }
    }

    return 0;
}

static int get_ticket_metadata(const char* id, const char* pKey)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    ticket parsedTicket;
    text_range key;
    size_t i;

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

    key = trim_line(pKey, 0, strlen(pKey));
    for (i = parsedTicket.metadataCount; i > 0; i -= 1) {
        const ticket_metadata* pMetadata = &parsedTicket.pMetadata[i - 1];

        if (text_range_equal(pFileData, pMetadata->key, pKey, key)) {
            fs_file_writef(STDOUT, "%.*s\n", (int)pMetadata->value.length, pFileData + pMetadata->value.offset);
            break;
        }
    }

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
            return author.length > 0 && !text_range_equal_string(pText, author, "<Insert Name>");
        }

        separatorOffset += 1;
    }

    return 0;
}

static size_t trim_trailing_newlines(const char* pText, size_t textLength)
{
    while (textLength > 0 && (pText[textLength - 1] == '\n' || pText[textLength - 1] == '\r')) {
        textLength -= 1;
    }

    return textLength;
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

    fileDataSize = trim_trailing_newlines(pFileData, fileDataSize);
    pSeparator = "\n\n---\n\n";

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

    result = replace_text_file(pFilePath, pUpdatedData, updatedDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    if (!has_comment_author(pEditedData, editedDataSize)) {
        fs_file_writef(STDERR, "Warning: Comment was saved without an author.\n");
    }

    return 0;
}

typedef struct
{
    char* pData;
    size_t size;
} text_buffer;

static int append_text(text_buffer* pBuffer, const char* pText, size_t textLength)
{
    char* pNewData;

    if (textLength == 0) {
        return 1;
    }

    pNewData = (char*)realloc(pBuffer->pData, pBuffer->size + textLength);
    if (pNewData == NULL) {
        return 0;
    }

    pBuffer->pData = pNewData;
    memcpy(pBuffer->pData + pBuffer->size, pText, textLength);
    pBuffer->size += textLength;
    return 1;
}

static const ticket_metadata* find_ticket_metadata(const char* pFileData, const ticket* pTicket, const char* pKeyText, text_range key)
{
    size_t metadataIndex;

    for (metadataIndex = pTicket->metadataCount; metadataIndex > 0; metadataIndex -= 1) {
        const ticket_metadata* pMetadata = &pTicket->pMetadata[metadataIndex - 1];

        if (text_range_equal(pFileData, pMetadata->key, pKeyText, key)) {
            return pMetadata;
        }
    }

    return NULL;
}

static int create_metadata_change_text(const char* pFileData, size_t fileDataSize, int argumentCount, char** ppArguments, int hasValues, text_buffer* pChanges)
{
    ticket parsedTicket;
    int argumentIndex;

    pChanges->pData = NULL;
    pChanges->size = 0;
    if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
        return 0;
    }

    for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex += 1) {
        const char* pArgument = ppArguments[argumentIndex];
        text_range key;
        text_range value;
        const ticket_metadata* pExistingMetadata;

        if (hasValues) {
            parse_metadata_set_argument(pArgument, &key, &value);
        } else {
            parse_metadata_key_only_argument(pArgument, &key);
            value.offset = 0;
            value.length = 0;
        }

        pExistingMetadata = find_ticket_metadata(pFileData, &parsedTicket, pArgument, key);
        if (hasValues && pExistingMetadata == NULL) {
            int appendFailed = !append_text(pChanges, pArgument + key.offset, key.length) || !append_text(pChanges, " set to ", 8) || !append_text(pChanges, pArgument + value.offset, value.length) || !append_text(pChanges, ".\n", 2);
            if (appendFailed) {
                return 0;
            }
        } else if (hasValues && !text_range_equal(pFileData, pExistingMetadata->value, pArgument, value)) {
            int appendFailed = !append_text(pChanges, pArgument + key.offset, key.length) || !append_text(pChanges, " changed from ", 14) || !append_text(pChanges, pFileData + pExistingMetadata->value.offset, pExistingMetadata->value.length) || !append_text(pChanges, " to ", 4) || !append_text(pChanges, pArgument + value.offset, value.length) || !append_text(pChanges, ".\n", 2);
            if (appendFailed) {
                return 0;
            }
        } else if (!hasValues && pExistingMetadata != NULL) {
            int appendFailed = !append_text(pChanges, pArgument + key.offset, key.length) || !append_text(pChanges, " removed (was ", 14) || !append_text(pChanges, pFileData + pExistingMetadata->value.offset, pExistingMetadata->value.length) || !append_text(pChanges, ").\n", 3);
            if (appendFailed) {
                return 0;
            }
        }
    }

    return 1;
}

static char* append_metadata_comment(const char* pFileData, size_t fileDataSize, const text_buffer* pChanges, int addGeneratedComment, const char* pMessage, size_t messageLength, size_t* pUpdatedDataSize)
{
    const char* pSeparator;
    size_t separatorLength;
    char* pCommentHeader;
    size_t commentHeaderLength;
    int authorFound;
    char* pUpdatedData;
    char* pWriteCursor;
    size_t generatedLength = addGeneratedComment ? pChanges->size : 0;
    size_t messageSeparatorLength = generatedLength > 0 && messageLength > 0 ? 1 : 0;
    size_t finalNewlineLength = messageLength > 0 && pMessage[messageLength - 1] != '\n' ? 1 : 0;

    pCommentHeader = create_comment_header(&commentHeaderLength, &authorFound);
    if (pCommentHeader == NULL) {
        return NULL;
    }

    if (!authorFound) {
        fs_file_writef(STDERR, "Warning: No author name was found for metadata comment.\n");
    }

    fileDataSize = trim_trailing_newlines(pFileData, fileDataSize);
    pSeparator = "\n\n---\n\n";
    separatorLength = strlen(pSeparator);
    *pUpdatedDataSize = fileDataSize + separatorLength + commentHeaderLength + 2 + generatedLength +
        messageSeparatorLength + messageLength + finalNewlineLength;
    pUpdatedData = (char*)malloc(*pUpdatedDataSize);
    if (pUpdatedData == NULL) {
        return NULL;
    }

    pWriteCursor = pUpdatedData;
    memcpy(pWriteCursor, pFileData, fileDataSize);
    pWriteCursor += fileDataSize;
    memcpy(pWriteCursor, pSeparator, separatorLength);
    pWriteCursor += separatorLength;
    memcpy(pWriteCursor, pCommentHeader, commentHeaderLength);
    pWriteCursor += commentHeaderLength;
    memcpy(pWriteCursor, "\n\n", 2);
    pWriteCursor += 2;
    if (generatedLength > 0) {
        memcpy(pWriteCursor, pChanges->pData, generatedLength);
        pWriteCursor += generatedLength;
    }
    if (messageSeparatorLength > 0) {
        pWriteCursor[0] = '\n';
        pWriteCursor += 1;
    }
    if (messageLength > 0) {
        memcpy(pWriteCursor, pMessage, messageLength);
        pWriteCursor += messageLength;
    }
    if (finalNewlineLength > 0) {
        pWriteCursor[0] = '\n';
    }

    return pUpdatedData;
}

static int set_ticket_metadata(const char* id, int argumentCount, char** ppArguments, int addGeneratedComment, const char* pMessage, size_t messageLength)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    int argumentIndex;
    text_buffer changes;
    int changed = 0;

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

    if (!create_metadata_change_text(pFileData, fileDataSize, argumentCount, ppArguments, 1, &changes)) {
        fs_file_writef(STDERR, "Failed to allocate metadata change data.\n");
        return 1;
    }

    for (argumentIndex = argumentCount; argumentIndex > 0; argumentIndex -= 1) {
        const char* pArgument = ppArguments[argumentIndex - 1];
        text_range key;
        text_range value;
        ticket parsedTicket;
        const ticket_metadata* pExistingMetadata = NULL;
        char* pUpdatedData;
        size_t updatedDataSize;
        size_t metadataIndex;

        parse_metadata_set_argument(pArgument, &key, &value);
        if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
            fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
            return 1;
        }

        for (metadataIndex = parsedTicket.metadataCount; metadataIndex > 0; metadataIndex -= 1) {
            const ticket_metadata* pMetadata = &parsedTicket.pMetadata[metadataIndex - 1];

            if (text_range_equal(pFileData, pMetadata->key, pArgument, key)) {
                pExistingMetadata = pMetadata;
                break;
            }
        }

        if (pExistingMetadata != NULL) {
            if (text_range_equal(pFileData, pExistingMetadata->value, pArgument, value)) {
                continue;
            }

            pUpdatedData = replace_text_range(pFileData, fileDataSize, pExistingMetadata->value, pArgument + value.offset, value.length, &updatedDataSize);
        } else {
            const char* pSuffix = parsedTicket.hasMetadataSection ? "\n" : "\n\n---\n\n";
            size_t suffixLength = strlen(pSuffix);
            size_t metadataLength = key.length + 2 + value.length + suffixLength;
            char* pMetadataText = (char*)malloc(metadataLength);
            text_range insertionRange;

            if (pMetadataText == NULL) {
                fs_file_writef(STDERR, "Failed to allocate metadata.\n");
                return 1;
            }

            memcpy(pMetadataText, pArgument + key.offset, key.length);
            memcpy(pMetadataText + key.length, ": ", 2);
            memcpy(pMetadataText + key.length + 2, pArgument + value.offset, value.length);
            memcpy(pMetadataText + key.length + 2 + value.length, pSuffix, suffixLength);

            insertionRange.offset = 0;
            insertionRange.length = 0;
            pUpdatedData = replace_text_range(pFileData, fileDataSize, insertionRange, pMetadataText, metadataLength, &updatedDataSize);
        }

        if (pUpdatedData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate updated ticket data.\n");
            return 1;
        }

        pFileData = pUpdatedData;
        fileDataSize = updatedDataSize;
        changed = 1;
    }

    if (!changed && messageLength == 0) {
        return 0;
    }

    if ((addGeneratedComment && changes.size > 0) || messageLength > 0) {
        char* pUpdatedData;
        size_t updatedDataSize;

        pUpdatedData = append_metadata_comment(pFileData, fileDataSize, &changes, addGeneratedComment, pMessage, messageLength, &updatedDataSize);
        if (pUpdatedData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate metadata comment data.\n");
            return 1;
        }

        pFileData = pUpdatedData;
        fileDataSize = updatedDataSize;
    }

    result = replace_text_file(pFilePath, pFileData, fileDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    return 0;
}

static text_range metadata_line_range(const char* pText, size_t textLength, const ticket_metadata* pMetadata)
{
    text_range line;
    size_t lineEnd = pMetadata->value.offset + pMetadata->value.length;

    line.offset = pMetadata->key.offset;
    while (line.offset > 0 && pText[line.offset - 1] != '\n') {
        line.offset -= 1;
    }

    while (lineEnd < textLength && pText[lineEnd] != '\n') {
        lineEnd += 1;
    }
    if (lineEnd < textLength) {
        lineEnd += 1;
    }

    line.length = lineEnd - line.offset;
    return line;
}

static int clear_ticket_metadata(const char* id, int argumentCount, char** ppArguments, int addGeneratedComment, const char* pMessage, size_t messageLength)
{
    fs_result result;
    char* pFilePath;
    char* pFileData;
    size_t fileDataSize;
    int argumentIndex;
    int changed = 0;
    text_buffer changes;

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

    if (!create_metadata_change_text(pFileData, fileDataSize, argumentCount, ppArguments, 0, &changes)) {
        fs_file_writef(STDERR, "Failed to allocate metadata change data.\n");
        return 1;
    }

    for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex += 1) {
        const char* pArgument = ppArguments[argumentIndex];
        text_range key;
        int found;

        parse_metadata_key_only_argument(pArgument, &key);

        do {
            ticket parsedTicket;
            size_t metadataIndex;

            found = 0;
            if (!parse_ticket(pFileData, fileDataSize, &parsedTicket)) {
                fs_file_writef(STDERR, "Failed to parse %s.\n", pFilePath);
                return 1;
            }

            for (metadataIndex = parsedTicket.metadataCount; metadataIndex > 0; metadataIndex -= 1) {
                const ticket_metadata* pMetadata = &parsedTicket.pMetadata[metadataIndex - 1];

                if (text_range_equal(pFileData, pMetadata->key, pArgument, key)) {
                    text_range line = metadata_line_range(pFileData, fileDataSize, pMetadata);
                    char* pUpdatedData;
                    size_t updatedDataSize;

                    pUpdatedData = replace_text_range(pFileData, fileDataSize, line, NULL, 0, &updatedDataSize);
                    if (pUpdatedData == NULL) {
                        fs_file_writef(STDERR, "Failed to allocate updated ticket data.\n");
                        return 1;
                    }

                    pFileData = pUpdatedData;
                    fileDataSize = updatedDataSize;
                    changed = 1;
                    found = 1;
                    break;
                }
            }
        } while (found);
    }

    if (!changed && messageLength == 0) {
        return 0;
    }

    if ((addGeneratedComment && changes.size > 0) || messageLength > 0) {
        char* pUpdatedData;
        size_t updatedDataSize;

        pUpdatedData = append_metadata_comment(pFileData, fileDataSize, &changes, addGeneratedComment, pMessage, messageLength, &updatedDataSize);
        if (pUpdatedData == NULL) {
            fs_file_writef(STDERR, "Failed to allocate metadata comment data.\n");
            return 1;
        }

        pFileData = pUpdatedData;
        fileDataSize = updatedDataSize;
    }

    result = replace_text_file(pFilePath, pFileData, fileDataSize);
    if (result != FS_SUCCESS) {
        fs_file_writef(STDERR, "Failed to update %s. %s.\n", pFilePath, fs_result_description(result));
        return 1;
    }

    return 0;
}

#if defined(TICKETFILE_ENABLE_TESTS)
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

static int test_text_range_file(const char* pCaseName, const char* pFileName, const char* pText, text_range range)
{
    char* pExpectedPath = get_test_path(pCaseName, pFileName);
    char* pExpected;
    size_t expectedSize;

    if (pExpectedPath == NULL || read_text_file(pExpectedPath, &pExpected, &expectedSize) != FS_SUCCESS) {
        return 0;
    }

    if (range.length == 0) {
        return expectedSize == 0;
    }

    return expectedSize == range.length + 1 && pExpected[expectedSize - 1] == '\n' && memcmp(pExpected, pText + range.offset, range.length) == 0;
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

    if (expectedSize != parsedTicket.status.length + 1 +
        (parsedTicket.shortDescription.length > 0 ? parsedTicket.shortDescription.length + 1 : 0)) {
        return 0;
    }

    if (memcmp(pExpected, pScript + parsedTicket.status.offset, parsedTicket.status.length) != 0 ||
        pExpected[parsedTicket.status.length] != '\n') {
        return 0;
    }

    {
        char* pDescriptionPath = get_test_path(pCaseName, "description.txt");
        fs_file* pDescriptionFile;

        if (pDescriptionPath != NULL && fs_file_open(NULL, pDescriptionPath, FS_READ, &pDescriptionFile) == FS_SUCCESS) {
            fs_file_close(pDescriptionFile);
            if (!test_text_range_file(pCaseName, "description.txt",          pScript, parsedTicket.description) ||
                !test_text_range_file(pCaseName, "short_description.txt",    pScript, parsedTicket.shortDescription) ||
                !test_text_range_file(pCaseName, "detailed_description.txt", pScript, parsedTicket.detailedDescription)) {
                return 0;
            }
        }
    }

    return parsedTicket.shortDescription.length == 0 ||
        (memcmp(pExpected + parsedTicket.status.length + 1,
            pScript + parsedTicket.shortDescription.offset, parsedTicket.shortDescription.length) == 0 &&
        pExpected[expectedSize - 1] == '\n');
}

static int run_metadata_command_test(const char* pCaseName)
{
    char* pCommandPath = get_test_path(pCaseName, "command.txt");
    char* pTicketPath = get_test_path(pCaseName, "ticket");
    char* pExpectedPath = get_test_path(pCaseName, "expected.txt");
    char* pCommandData;
    char* pTicketData;
    char* pExpectedData;
    size_t commandDataSize;
    size_t ticketDataSize;
    size_t expectedDataSize;
    char* ppArguments[32];
    int argumentCount = 0;
    size_t cursor = 0;
    char temporaryPath[1024];
    const char* pPreviousTicketsFolder;
    char* pTemporaryTicketPath;
    char* pActualData;
    size_t actualDataSize;
    metadata_command_options options;
    int expectedSuccess = 1;
    int result;
    int passed;

    if (pCommandPath == NULL || pTicketPath == NULL || pExpectedPath == NULL ||
        read_text_file(pCommandPath,  &pCommandData,  &commandDataSize ) != FS_SUCCESS ||
        read_text_file(pTicketPath,   &pTicketData,   &ticketDataSize  ) != FS_SUCCESS ||
        read_text_file(pExpectedPath, &pExpectedData, &expectedDataSize) != FS_SUCCESS) {
        return 0;
    }

    {
        static const char placeholder[] = "<COMMENT_HEADER>";
        char* pPlaceholder = strstr(pExpectedData, placeholder);

        if (pPlaceholder != NULL) {
            char* pCommentHeader;
            size_t commentHeaderLength;
            int authorFound;
            text_range placeholderRange;
            char* pExpandedExpectedData;
            size_t expandedExpectedDataSize;

            pCommentHeader = create_comment_header(&commentHeaderLength, &authorFound);
            if (pCommentHeader == NULL) {
                return 0;
            }

            placeholderRange.offset = (size_t)(pPlaceholder - pExpectedData);
            placeholderRange.length = sizeof(placeholder) - 1;
            pExpandedExpectedData = replace_text_range(pExpectedData, expectedDataSize, placeholderRange, pCommentHeader, commentHeaderLength, &expandedExpectedDataSize);
            if (pExpandedExpectedData == NULL) {
                return 0;
            }

            pExpectedData = pExpandedExpectedData;
            expectedDataSize = expandedExpectedDataSize;
        }
    }

    while (cursor < commandDataSize) {
        size_t lineStart = cursor;

        while (cursor < commandDataSize && pCommandData[cursor] != '\n' && pCommandData[cursor] != '\r') {
            cursor += 1;
        }

        if (cursor > lineStart) {
            if (argumentCount == (int)(sizeof(ppArguments) / sizeof(ppArguments[0]))) {
                return 0;
            }

            ppArguments[argumentCount] = pCommandData + lineStart;
            argumentCount += 1;
        }

        while (cursor < commandDataSize && (pCommandData[cursor] == '\n' || pCommandData[cursor] == '\r')) {
            pCommandData[cursor] = '\0';
            cursor += 1;
        }
    }

    if (argumentCount < 2 || fs_mktmp("ticketfile-test", temporaryPath, sizeof(temporaryPath), FS_MKTMP_DIR) != FS_SUCCESS) {
        return 0;
    }

    {
        char* pResultPath = get_test_path(pCaseName, "result.txt");
        char* pResultData;
        size_t resultDataSize;

        if (pResultPath != NULL && read_text_file(pResultPath, &pResultData, &resultDataSize) == FS_SUCCESS) {
            text_range expectedResult = trim_line(pResultData, 0, resultDataSize);
            expectedSuccess = text_range_equal_string(pResultData, expectedResult, "success");
        }
    }

    pPreviousTicketsFolder = g_pTicketsFolder;
    g_pTicketsFolder = temporaryPath;

    pTemporaryTicketPath = get_ticket_path("1", 1);
    if (pTemporaryTicketPath == NULL || write_text_file(pTemporaryTicketPath, pTicketData, ticketDataSize) != FS_SUCCESS) {
        g_pTicketsFolder = pPreviousTicketsFolder;
        return 0;
    }

    if (!parse_metadata_command_options(argumentCount - 1, ppArguments + 1, &options)) {
        result = 1;
    } else if (options.metadataArgumentCount < 1 || !validate_metadata_arguments(ppArguments[0], options.metadataArgumentCount, ppArguments + 1)) {
        result = 1;
    } else if (strcmp(ppArguments[0], "set") == 0) {
        result = set_ticket_metadata("1", options.metadataArgumentCount, ppArguments + 1, options.addGeneratedComment, options.pMessage, options.messageLength);
    } else if (strcmp(ppArguments[0], "clear") == 0) {
        result = clear_ticket_metadata("1", options.metadataArgumentCount, ppArguments + 1, options.addGeneratedComment, options.pMessage, options.messageLength);
    } else {
        result = 1;
    }

    passed = ((result == 0) == expectedSuccess && read_text_file(pTemporaryTicketPath, &pActualData, &actualDataSize) == FS_SUCCESS && actualDataSize == expectedDataSize && memcmp(pActualData, pExpectedData, expectedDataSize) == 0);
    fs_remove(NULL, pTemporaryTicketPath, 0);
    fs_remove(NULL, temporaryPath, 0);
    g_pTicketsFolder = pPreviousTicketsFolder;

    return passed;
}

static int run_test_case(const char* pCaseName)
{
    char* pCommandPath = get_test_path(pCaseName, "command.txt");
    fs_file* pCommandFile;
    fs_result result;

    if (pCommandPath == NULL) {
        return 0;
    }

    result = fs_file_open(NULL, pCommandPath, FS_READ, &pCommandFile);
    if (result == FS_SUCCESS) {
        fs_file_close(pCommandFile);
        return run_metadata_command_test(pCaseName);
    }

    return run_parser_test(pCaseName);
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
        passed = run_test_case(ppCaseNames[i]);
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
#endif


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

    if (strcmp(argv[argumentIndex], "--directory") == 0 || strcmp(argv[argumentIndex], "-d") == 0) {
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
        return list_tickets(argc - argumentIndex - 1, argv + argumentIndex + 1);
    }

    if (strcmp(argv[argumentIndex], "show") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The show command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return show_ticket(argv[argumentIndex + 1]);
    }

    if (strcmp(argv[argumentIndex], "description") == 0) {
        int descriptionPart = 0;

        if (argc < argumentIndex + 2 || argc > argumentIndex + 3) {
            fs_file_writef(STDERR, "The description command requires one ticket ID and one optional selection.\n");
            print_usage(argv[0]);
            return 1;
        }

        if (argc == argumentIndex + 3) {
            if (strcmp(argv[argumentIndex + 2], "--short") == 0) {
                descriptionPart = 1;
            } else if (strcmp(argv[argumentIndex + 2], "--detailed") == 0) {
                descriptionPart = 2;
            } else {
                fs_file_writef(STDERR, "Unknown description option: %s.\n", argv[argumentIndex + 2]);
                print_usage(argv[0]);
                return 1;
            }
        }

        return show_ticket_description(argv[argumentIndex + 1], descriptionPart);
    }

    if (strcmp(argv[argumentIndex], "edit") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The edit command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return edit_ticket(argv[argumentIndex + 1]);
    }

    if (strcmp(argv[argumentIndex], "get") == 0) {
        text_range key;

        if (argc != argumentIndex + 3) {
            fs_file_writef(STDERR, "The get command requires one ticket ID and one metadata key.\n");
            print_usage(argv[0]);
            return 1;
        }

        if (!parse_metadata_key_only_argument(argv[argumentIndex + 2], &key)) {
            fs_file_writef(STDERR, "The get command requires a non-empty metadata key without a colon.\n");
            return 1;
        }

        return get_ticket_metadata(argv[argumentIndex + 1], argv[argumentIndex + 2]);
    }

    if (strcmp(argv[argumentIndex], "set") == 0 || strcmp(argv[argumentIndex], "clear") == 0) {
        metadata_command_options options;
        int optionArgumentCount = argc - argumentIndex - 2;
        int hasValues = strcmp(argv[argumentIndex], "set") == 0;

        if (!parse_metadata_command_options(optionArgumentCount, argv + argumentIndex + 2, &options)) {
            return 1;
        }

        if (options.metadataArgumentCount < 1) {
            fs_file_writef(STDERR, "The %s command requires one ticket ID and at least one metadata argument.\n", argv[argumentIndex]);
            print_usage(argv[0]);
            return 1;
        }

        if (!validate_metadata_arguments(argv[argumentIndex], options.metadataArgumentCount, argv + argumentIndex + 2)) {
            return 1;
        }

        if (hasValues) {
            return set_ticket_metadata(argv[argumentIndex + 1], options.metadataArgumentCount, argv + argumentIndex + 2, options.addGeneratedComment, options.pMessage, options.messageLength);
        }

        return clear_ticket_metadata(argv[argumentIndex + 1], options.metadataArgumentCount, argv + argumentIndex + 2, options.addGeneratedComment, options.pMessage, options.messageLength);
    }

    if (strcmp(argv[argumentIndex], "new") == 0) {
        const char* pMessage = NULL;
        const char* pDescriptionPath = NULL;

        if (argc > argumentIndex + 1) {
            if (strcmp(argv[argumentIndex + 1], "-m") != 0 && strcmp(argv[argumentIndex + 1], "--message") != 0 && strcmp(argv[argumentIndex + 1], "-F") != 0 && strcmp(argv[argumentIndex + 1], "--file") != 0) {
                fs_file_writef(STDERR, "Unknown new option: %s.\n", argv[argumentIndex + 1]);
                print_usage(argv[0]);
                return 1;
            }

            if (argc <= argumentIndex + 2) {
                fs_file_writef(STDERR, "The %s option requires a value.\n", argv[argumentIndex + 1]);
                print_usage(argv[0]);
                return 1;
            }

            if (argc != argumentIndex + 3) {
                fs_file_writef(STDERR, "The new command has too many arguments.\n");
                print_usage(argv[0]);
                return 1;
            }

            if (strcmp(argv[argumentIndex + 1], "-F") == 0 || strcmp(argv[argumentIndex + 1], "--file") == 0) {
                pDescriptionPath = argv[argumentIndex + 2];
            } else {
                pMessage = argv[argumentIndex + 2];
            }
        }

        return create_ticket(pMessage, pDescriptionPath);
    }

    if (strcmp(argv[argumentIndex], "comment") == 0) {
        if (argc != argumentIndex + 2) {
            fs_file_writef(STDERR, "The comment command requires one ticket ID.\n");
            print_usage(argv[0]);
            return 1;
        }

        return comment_on_ticket(argv[argumentIndex + 1]);
    }

    #if defined(TICKETFILE_ENABLE_TESTS)
    {
        if (strcmp(argv[argumentIndex], "--test") == 0) {
            if (argc > argumentIndex + 2) {
                fs_file_writef(STDERR, "The --test option takes no more than one case.\n");
                return 1;
            }

            return run_tests(argc == argumentIndex + 2 ? argv[argumentIndex + 1] : NULL);
        }
    }
    #endif

    if (strcmp(argv[argumentIndex], "--help") == 0 || strcmp(argv[argumentIndex], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[argumentIndex], "--version") == 0 || strcmp(argv[argumentIndex], "-v") == 0) {
        print_version();
        return 0;
    }

    fs_file_writef(STDERR, "Unknown command: %s\n", argv[argumentIndex]);
    print_usage(argv[0]);

    return 1;
}
