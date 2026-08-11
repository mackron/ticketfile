# Plain Text Ticket Management for Visual Studio Code

Ticket Explorer manages simple tickets stored as human-readable plain-text
files. Ticket files remain usable with normal text editors and version-control
tools.

## Features

- View tickets in Open, Closed, and Uncategorized groups.
- Create, open, close, reopen, comment on, and delete tickets.
- Filter tickets by metadata such as `status:open`.
- Refresh the view automatically when ticket files change.
- Configure a ticket directory for each workspace.

## Getting started

Open a folder in Visual Studio Code, then select **Tickets** in the Activity Bar.
By default, Ticket Explorer reads ticket files from the `tickets` directory in
the first workspace folder.

Use the `ticketfile.ticketsFolder` workspace setting to select a different
directory. The path must be relative to the first workspace folder.

See [ticket 0](https://github.com/mackron/ticketfile/blob/master/tickets/0) for
an example of the optional ticket format.

## License

Your choice of either the Unlicense or MIT No Attribution.
