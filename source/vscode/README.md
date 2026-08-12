# Plain Text Ticket Management for Visual Studio Code

Ticket Explorer manages simple tickets stored as human-readable plain-text
files. Ticket files remain usable with normal text editors and version-control
tools.

## Features

- View tickets in Open, Closed, and Uncategorized groups.
- Create, open, change status, comment on, and delete tickets.
- Filter tickets by metadata such as `status:open`.
- Refresh the view automatically when ticket files change.
- Configure a ticket directory for each workspace.

## Getting Started

Open a folder in Visual Studio Code, then select **Tickets** in the Activity Bar.
By default, Ticket Explorer reads ticket files from the `tickets` directory in
the first workspace folder.

Use the `ticketfile.ticketsFolder` workspace setting to select a different
directory. The path must be relative to the first workspace folder.

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
