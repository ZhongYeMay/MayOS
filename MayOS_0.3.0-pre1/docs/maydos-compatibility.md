# MayDOS 1.0.0 → MayOS native compatibility apps

Original source: user-uploaded MayDOS_v1.0.0.zip. This is a new native C
implementation, not a direct Python, Tkinter, or Windows console runtime.

## Feature mapping

| MayDOS feature | MayOS implementation |
|---|---|
| CALC | Native Calculator window or calc EXPRESSION in MayShell |
| MINESWEEPER | Native Minesweeper window or minesweeper easy/medium/expert/custom |
| NOTEPAD DOS | notepad dos displays CLI help; ls/cat/touch/write/save are already available |
| NOTEPAD GUI | notepad opens native Editor; .txt and old .MayNote supported |
| USEBOOK, SYSVER, CLS | MayShell commands usebook, sysver, cls |
| ADD, LIST, DEL | Extensions window and add [file], list, del FILE |
| SHUT, EXIT | shut confirm powers off; exit hides Terminal |

The source version's usebook.txt described LIST and DEL, but the shipped
MayDOS_System.py did not implement them. MayOS supports these operations on
staged data files, not arbitrary executables.

## Native GUI

Open Applications using Super/Windows key, then choose Calculator,
Minesweeper or Extension Library. MayOS keeps its existing four-icon Dock
and approved monochrome window components. Calculator accepts keyboard input.
Minesweeper supports left reveal, right flag, safe first click, proper
adjacent counts, flood-fill and win/loss.

Beginner: 9x9 / 10 mines. Intermediate: 16x16 / 40. Expert: 16x30 / 99,
landscape rotation of the original 30x16. Custom: max 480 cells with
5–20 rows, 5–30 columns, and 15–25% mines.

The calculator uses a bounded decimal parser (up to 3 fractional places)
for +, -, *, /, unary signs and parentheses. Python eval() is not used.

## Old .MayNote interoperability

Notepad_GUI saved UTF-8 text as Base64 and used the .MayNote extension.
MayOS Editor transparently decodes on open and encodes on save, rejecting
malformed input without modifying the source file. Base64 is not encryption.
MayFS has a maximum of 4095 bytes per file.

## Extensions: data-only migration

1. Open Extensions (or run apps) and choose Open import directory.
2. Place data files inside Documents/AddFiles, using Files or MayShell.
3. Select Import all or run add NAME in Terminal.
4. Imported files are stored in Documents/ThirdPartyFile and shown by list.
5. Use the GUI delete action or del NAME to move data safely to Trash.

Legacy Windows .exe/.lnk files and Python/Tkinter programs cannot execute
in the custom MayKernel. This version imports only their bytes as data.
Native app loading, package signing and sandboxing are future work.

## Storage and validation

MayKernel persists files only when a valid dedicated MayFS disk is present,
otherwise it uses RAM. Linux GUI has a separate temporary RAM-backed file
model and does not share files with MayKernel.

Tests: python3 scripts/test_maydos_tools.py; make test; make test-dual;
make test-uefi. QEMU screenshots are captured from a real running guest.
Do not merge or publish a release until required CI and matching ISO/video
artifacts are successfully verified.
