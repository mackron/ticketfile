This repository contains tools for working with tickets in human editable plain text files. It
includes a VS Code extension for humans, and a CLI which is mostly useful for clankers. Humans
would typically edit ticket files in a normal text editor or through the VS Code extension.

Tickets go into a sub-folder (defaults to "tickets"), with each one named with the ticket's numeric
ID. See the "tickets" folder in this repository for an example. Tickets are simple plain text, with
optional metadata at the top. Below is a typical example of what I use in my projects:

```
status: open

---

Error with login screen.

If the user hits tab from the username field, it puts keyboard focus on
the "Login" button instead of the password field.
```

The tools in this repository recognize the "status" metadata at the top. Status values are not
restricted. The VS Code extension can map configured status values to groups. Metadata is optional.
A ticket could also look like this:

```
Possible null pointer dereference when loading a file.
```

In this example there is no metadata and no detailed description. This is still valid.

The first line in the description is the short description that tools display.

Comments can be added by just separating them with "---":

```
status: closed

---

Error with login screen.

If the user hits tab from the username field, it puts keyboard focus on
the "Login" button instead of the password field.

---

2026-08-11 - David Reid

This was caused by incorrect tab ordering in the login form.
```

See [Ticket 0](tickets/0) for an example and a more complete description of the optional formatting
that the tools support.


## CLI Usage

Run `ticket` without arguments to show command usage. Tickets are read from the `tickets`
directory by default. Use `-d` or `--directory` before a command to select another directory:

```
ticket --directory path/to/tickets list
```

Use `list` to show all tickets. Add metadata filters to show tickets that match every valid
filter:

```
ticket list status:review "assignee:David Reid"
```

Use `show` to write a complete ticket to standard output, or `edit` to open it in `VISUAL`,
`EDITOR`, or the default editor:

```
ticket show 123
ticket edit 123
```

Use `get` to write one metadata value. A missing key writes no output and still returns success:

```
ticket get 123 status
```

Use `set` to add or replace one or more metadata values atomically. Quote an argument when its
value contains spaces:

```
ticket set 123 status:review "assignee:David Reid"
```

When an existing status changes, `set` adds a dated history entry. Use `--no-comment` as the final
argument to suppress this entry:

```
ticket set 123 status:review --no-comment
```

Use `clear` to remove one or more metadata values atomically. Clearing a status does not add a
history entry:

```
ticket clear 123 status assignee
```

Use `new` without arguments to create an open ticket in an editor. Use `-m` or `--message` to
create one from an inline description:

```
ticket new --message "Fix incorrect tab order."
```

Use `-F` or `--file` to read the description from a file:

```
ticket new --file description.txt
```

Use `comment` to open an editor and append a dated comment:

```
ticket comment 123
```

Use `-h` or `--help` to show command usage. Use `-v` or `--version` to show version information.


## Installing

Download the archive for your operating system from the GitHub Releases page. Extract
`ticket` (`ticket.exe` on Windows), and put it in a directory listed in your `PATH`.

To install the VS Code extension, download the `.vsix` file from the same release. In VS Code, open
the Extensions view, select **Views and More Actions**, and then select **Install from VSIX**. You
can also install it from a terminal:

```
code --install-extension ticketfile-<version>.vsix
```


## Building

For the CLI, compile `source/cli/ticketfile.c` or use CMake. The VS Code extension is in
`source/vscode`.

To package the VS Code extension, install Node.js and run:

```
cd source/vscode
npm run package
```

The command creates `ticketfile-<version>.vsix` in `build/vscode`. It reads the version from
`source/ticketfile_version.h`, copies the extension README and root license into a temporary
package, and removes temporary files when it finishes. The packaging tool can be downloaded by
`npx` when it is not already installed.

Update the three macros in `source/ticketfile_version.h` for a release. CMake, the CLI, and the
extension packaging script all use this version. The packaging script also updates the version in
`source/vscode/package.json`.


## Releasing

Create one ticket with `release-notes:<version>` metadata for the release version without the `v`
tag prefix:

```text
status: open
release-notes: 1.1.0

---

Release Notes - v1.1.0

## General

- Describe a user-facing change.
```

The first description line is the ticket short description. It is not part of the published notes.
Write curated release notes as Markdown after that line. Ticket comments after the next `---`
separator are also not published.

Update the version macros in `source/ticketfile_version.h`, commit the release-note ticket and
version change, and push `master`. Then run:

```
node release.js
```

The script requires a clean working tree and a local `master` that matches `origin/master`. It
downloads current tags and requires a successful GitHub Build workflow for the current commit. It
requires exactly one non-empty release-note ticket for the header version. It then confirms that
the header version is newer than all existing release tags and asks for confirmation. Finally, it
creates and pushes an annotated `v<major>.<minor>.<patch>` tag. GitHub automation extracts the same
ticket body and publishes it with release files.

The CI check uses the GitHub API. Set `GITHUB_TOKEN` if API authentication is required or if the
unauthenticated API rate limit is too low.

Use `node release.js --check` to validate the release without creating a tag.
Use `node release.js --yes` to skip the confirmation prompt.
Use `node release.js --write-notes <path>` to inspect extracted notes for the header version.


## License

Your choice of either public domain or [MIT No Attribution](https://github.com/aws/mit-0).
