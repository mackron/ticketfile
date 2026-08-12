This repository contains tools for working with tickets in human editable plain text files. It includes a VS Code extension for humans, and a CLI which is mostly useful for clankers. Humans would typically just edit ticket files in a normal text editor or through the VS Code extension.

Tickets go into a sub-folder (defaults to "tickets"), with each one named with the ticket's numeric ID. See the "tickets" folder in this repository for an example. Tickets are simple and plain text, with optional metadata at the top. Below is a typical example of what I use in my projects:

```
status: open

---

Error with login screen.

If the user hits tab from the username field, it puts keyboard focus on
the "Login" button instead of the password field.
```

The tools in this repository will recognize the "status" metadata at the top and categorize it appropriately. The recognized statuses are "open" and "closed". Metadata is optional. A ticket could also look like this:

```
Possible null pointer dereference when loading a file.
```

In this example there is no metadata and no detailed description. This is still valid.

The first line in the description is considered the short description and will be what's displayed by tools.

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

See [Ticket 0](tickets/0) for an example and a more complete description on the (optional) formatting supported by the tools in this repository.


## CLI Usage

Use `get` to write one metadata value. A missing key writes no output and still
returns success:

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


## Installing

Download the archive for your operating system from the GitHub Releases page.
Extract `ticket` (`ticket.exe` on Windows), and put it in a directory listed in
your `PATH`.

To install the VS Code extension, download the `.vsix` file from the same
release. In VS Code, open the Extensions view, select **Views and More
Actions**, and then select **Install from VSIX**. You can also install it from a
terminal:

```
code --install-extension ticketfile-<version>.vsix
```


## Building

For the CLI, just compile source/cli/ticketfile.c or use CMake. The VS Code extension is in source/vscode.

To package the VS Code extension, install Node.js and run:

```
cd source/vscode
npm run package
```

The command creates `ticketfile-<version>.vsix` in `build/vscode`. It reads the version from `source/ticketfile_version.h`, copies the extension README and root license into a temporary package, and removes temporary files when it finishes. The packaging tool can be downloaded by `npx` when it is not already installed.

Update the three macros in `source/ticketfile_version.h` for a release. CMake, the CLI, and the extension packaging script all use this version. The packaging script also updates the version stored in `source/vscode/package.json`.


## Releasing

Update the version macros in `source/ticketfile_version.h`, commit the change,
and push `master`. Then run:

```
node release.js
```

The script requires a clean working tree and a local `master` that matches
`origin/master`. It downloads current tags and requires a successful GitHub
Build workflow for the current commit. It then confirms that the header version
is newer than all existing release tags and asks for confirmation. Finally, it
creates and pushes an annotated `v<major>.<minor>.<patch>` tag. The tag starts
GitHub release automation.

The CI check uses the GitHub API. Set `GITHUB_TOKEN` if API authentication is
required or if the unauthenticated API rate limit is too low.

Use `node release.js --check` to validate the release without creating a tag.
Use `node release.js --yes` to skip the confirmation prompt.


## License

Your choice of either public domain or [MIT No Attribution](https://github.com/aws/mit-0).
