# Parser tests

Run all tests from the source root:

    ticket --test

Run one test with its full folder name or three-digit number:

    ticket --test 001_parse_open
    ticket --test 001

Each test is in `tests/cases/<NNN_name>/`. Each folder contains
`ticket` and `expectation.txt`.

`expectation.txt` supports these values:

- `parse_success`: Parsing must succeed. `expected.txt` contains the parsed
  status on line 1 and, when present, the parsed short description on line 2.
- `parse_error`: Parsing must fail.

Tests run in folder-name order. The runner prints one result for each selected
case and a final summary.

Run tests through CMake and CTest:

    cmake -S . -B build -DBUILD_TESTING=ON
    cmake --build build
    ctest --test-dir build --output-on-failure

Run the same parser cases against the VS Code extension parser:

    cd source/vscode
    npm test
