const assert = require("assert");
const manifest = require("./package.json");
const { loadStatusGroups, validateStatusGroups } = require("./ticket_configuration");

const manifestDefault = manifest.contributes.configuration.properties["ticketfile.statusGroups"].default;

function configuration(values)
{
    return {
        inspect: () => values
    };
}

assert.strictEqual(validateStatusGroups(manifestDefault), undefined);
assert.strictEqual(validateStatusGroups("open"), "Ticket status groups must be an array.");
assert.match(validateStatusGroups([null]), /must be an object/);
assert.match(validateStatusGroups([{ label: "", status: "open", expanded: true }]), /non-empty label/);
assert.match(validateStatusGroups([{ label: "Open", status: "", expanded: true }]), /non-empty status/);
assert.match(validateStatusGroups([{ label: "Open", status: "open", expanded: "yes" }]), /Boolean expanded/);
assert.match(validateStatusGroups([
    { label: "First", status: "open", expanded: true },
    { label: "Second", status: "open", expanded: false }
]), /duplicate status/);

const userGroups = [
    { label: "Review", status: "review", expanded: true },
    { label: "Done", status: "done", expanded: false }
];
const workspaceGroups = [
    { label: "Workspace", status: "workspace", expanded: true }
];

assert.deepStrictEqual(loadStatusGroups(configuration({
    defaultValue: manifestDefault,
    globalValue: userGroups,
    workspaceValue: workspaceGroups
})).groups, workspaceGroups);
assert.deepStrictEqual(loadStatusGroups(configuration({
    defaultValue: manifestDefault,
    globalValue: userGroups
})).groups, userGroups);
assert.deepStrictEqual(loadStatusGroups(configuration({
    defaultValue: manifestDefault
})).groups, manifestDefault);

const invalidConfiguration = loadStatusGroups(configuration({
    defaultValue: manifestDefault,
    workspaceValue: [
        { label: "First", status: "duplicate", expanded: true },
        { label: "Second", status: "duplicate", expanded: false }
    ]
}));

assert.match(invalidConfiguration.error, /duplicate status/);
assert.deepStrictEqual(invalidConfiguration.groups, manifestDefault);

console.log("PASS: ticket configuration");
