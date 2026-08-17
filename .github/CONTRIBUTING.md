Contributing to drone
=====================

We welcome any and all help in the form of bug reports, fixes, patches
for new features, *preferably as GitHub pull requests*.  Other methods
are of course also possible: emailing the maintainer a patch or even a
raw file, or simply emailing a feature request or an alert of a problem.
However, email questions/requests/alerts always risk memory exhaustion
on the part of the maintainer(s).

If you are unsure of what to do, or how to implement an idea or bug fix,
open an issue with the title `"[RFC: Unsure if this is a bug ... ?"`, or
similar, so we can discuss it.  Talking about the code first is the best
way to get started before submitting a pull request.

Either way, when sending an email, patch, or pull request, start by
stating the version the change is made against, what it does, and most
importantly -- why.


Building and Testing
--------------------

See the [README][] for the build steps -- note that the FreeRTOS kernel
and the lwIP stack are submodules, so a plain clone will not build.

Please run `make check` before submitting, and add a test for the slice
of behavior you change.  The self-tests are plain shell scripts, one per
slice, and need nothing installed; [test/test.mk][tests] documents the
runner and its exit-code convention.


Coding Style
------------

> **Tip:** Always submit code that follows the style of surrounding code!

The app and port code is [Linux KNF][KNF].  There is no need to eyeball
it -- `.clang-format` holds the rules and `FMT_FILES` in the [Makefile][]
holds the list of files they apply to:

    make fmt


Commit Messages
---------------

Commit messages exist to track *why* a change was made.  Try to be as
clear and concise as possible in your commit messages, and always, be
proud of your work and set up a proper GIT identity for your commits:

    git config --global user.name "Jane Doe"
    git config --global user.email jane.doe@example.com

Example commit message from the [Pro Git][gitbook] online book, notice
how `git commit -s` is used to automatically add a `Signed-off-by`:

    Brief, but clear and concise summary of changes

    More detailed explanatory text, if necessary.  Wrap it to about 72
    characters or so.  In some contexts, the first line is treated as
    the subject of an email and the rest of the text as the body.  The
    blank line separating the summary from the body is critical (unless
    you omit the body entirely); tools like rebase can get confused if
    you run the two together.

    Further paragraphs come after blank lines.

     - Bullet points are okay, too

     - Typically a hyphen or asterisk is used for the bullet, preceded
       by a single space, with blank lines in between, but conventions
       vary here

    Signed-off-by: Jane Doe <jane.doe@example.com>


Code of Conduct
---------------

It is expected of everyone engaging in the project to, in the words of
Bill & Ted; [be excellent to each other][conduct].


[README]:   https://github.com/troglobit/drone/blob/main/README.md
[tests]:    https://github.com/troglobit/drone/blob/main/test/test.mk
[Makefile]: https://github.com/troglobit/drone/blob/main/Makefile
[KNF]:      https://en.wikipedia.org/wiki/Kernel_Normal_Form
[gitbook]:  https://git-scm.com/book/ch5-2.html
[conduct]:  https://github.com/troglobit/drone/blob/main/.github/CODE-OF-CONDUCT.md
