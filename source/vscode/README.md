# Plain Text Ticket Management for Visual Studio Code

Ticketfile manages simple tickets stored as human-readable plain-text
files. Ticket files remain usable with normal text editors and version-control
tools.

## Features

- View tickets in Open, Closed, and Uncategorized groups.
- Create, open, assign, change status, comment on, and delete tickets.
- Filter tickets by metadata such as `status:open`.
- Refresh the view automatically when ticket files change.
- Configure one or more ticket directories for each workspace.

## Getting Started

Open a folder in Visual Studio Code, then select **Tickets** in the Activity Bar.
By default, Ticketfile reads ticket files from the `tickets` directory in
the first workspace folder.

Use `ticketfile.ticketFolders` to configure ticket directories. Each entry requires a display
label and a path relative to the first workspace folder:

```json
{
    "ticketfile.ticketFolders": [
        {
            "label": "Tickets",
            "path": "tickets"
        },
        {
            "label": "Private",
            "path": "tickets/private"
        }
    ]
}
```

With one configured directory, status groups remain at the root of the ticket tree. With multiple
directories, each directory has a top-level tree item that contains its status groups. Ticket IDs
can overlap between directories.

When creating a ticket, Ticketfile uses the directory from the selected tree item when
possible. If multiple directories are configured and no directory can be inferred, it asks you to
select one.

Set `ticketfile.ticketFolders` in user settings to use the same directories in all projects. A
workspace setting replaces the complete user list. User and workspace arrays do not merge.

## Assignees

Use `ticketfile.assignees` to add names to the Assign To menu in display order:

```json
{
    "ticketfile.assignees": [
        "David Reid",
        "Clanker"
    ]
}
```

Use **Assign To...** from a ticket context menu or the command palette. Select a configured name,
select **Enter Name...** for a free-form name, or select **Clear Assignee** to remove assignee
metadata.

Set `ticketfile.assignees` in user settings to use the same names in all projects. A workspace
setting replaces the complete user list. User and workspace arrays do not merge.

## Status Groups

Use `ticketfile.statusGroups` to configure groups in the ticket tree. The array
order sets the display order. Each group requires a display label, an exact
status value, and its initial expansion state:

```json
{
    "ticketfile.statusGroups": [
        {
            "label": "Open",
            "status": "open",
            "expanded": true
        },
        {
            "label": "Ready for Review",
            "status": "review",
            "expanded": true
        },
        {
            "label": "Closed",
            "status": "closed",
            "expanded": false
        }
    ],
    "ticketfile.initialStatus": "open"
}
```

Set these values in user settings to use them in all projects. A workspace
setting replaces the complete user group list. User and workspace arrays do not
merge.

Tickets with a missing or unmapped status appear in the permanent
Uncategorized group. Each group shows its current filtered ticket count.

`ticketfile.initialStatus` sets the status metadata for a new ticket. Its
default value is `open`. Set it to an empty string to create new tickets without
status metadata.

See [ticket 0](https://github.com/mackron/ticketfile/blob/master/tickets/0) for
an example of the optional ticket format.

## License

Your choice of either the Unlicense or MIT No Attribution.
