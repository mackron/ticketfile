const assert = require("assert");
const fs = require("fs");
const path = require("path");
const { findStatusRange, parseMetadataFilters, parseTicket, ticketMatchesFilters } = require("./ticket_parser");

const casesPath = path.join(__dirname, "..", "..", "tests", "cases");
const caseNames = fs.readdirSync(casesPath, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name)
    .sort();

let passedCount = 0;
const failedCases = [];

for (const caseName of caseNames) {
    const casePath = path.join(casesPath, caseName);
    const ticket = fs.readFileSync(path.join(casePath, "ticket"), "utf8");
    const expectation = fs.readFileSync(path.join(casePath, "expectation.txt"), "utf8").trim();

    try {
        const result = parseTicket(ticket);

        if (expectation === "parse_success") {
            const expected = fs.readFileSync(path.join(casePath, "expected.txt"), "utf8");
            const actual = result.title === undefined ? `${result.status || ""}\n` : `${result.status || ""}\n${result.title}\n`;

            assert.strictEqual(actual, expected);
        } else if (expectation === "parse_error") {
            assert.fail("The relaxed parser does not reject ticket text.");
        } else {
            assert.fail(`Unknown expectation: ${expectation}`);
        }

        passedCount += 1;
        console.log(`PASS: ${caseName}`);
    } catch (error) {
        failedCases.push(caseName);
        console.log(`FAIL: ${caseName}`);
        console.log(`  ${error.message}`);
    }
}

const filterTicket = parseTicket("assignee: David Reid\npriority: high\nstatus: open\n\n---\n\nFilter test.\n");
assert.deepStrictEqual(parseMetadataFilters("status:open \"assignee:David Reid\" invalid :missing missing:"), [
    { key: "status", value: "open" },
    { key: "assignee", value: "David Reid" }
]);
assert.strictEqual(ticketMatchesFilters(filterTicket, parseMetadataFilters("status:open priority:high")), true);
assert.strictEqual(ticketMatchesFilters(filterTicket, parseMetadataFilters("status:closed")), false);
assert.deepStrictEqual(findStatusRange("status: open\n\n---\n\nRange test.\n"), { offset: 8, length: 4 });
assert.strictEqual(findStatusRange("Plain ticket.\n"), undefined);
assert.strictEqual(parseTicket("assignee: David Reid\n\n---\n\nNo status.\n").hasMetadataSection, true);

console.log(`SUMMARY: ${passedCount}/${caseNames.length} passed`);

if (failedCases.length > 0) {
    console.log("FAILED CASES:");
    for (const caseName of failedCases) {
        console.log(`  ${caseName}`);
    }
    process.exitCode = 1;
}
