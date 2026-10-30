# Releasing

A release is made by GitHub Actions from a tag. The file that users download is always called `Transmission-<version>.elf`.

1. Make sure `main` is green (the workflows **Build** and **Tests**).
2. Write the notes: in `CHANGELOG.md` the heading `## [<version>] - <date>` with the list of changes under it (this text becomes the notes of the release;
   `scripts/release-notes.sh <version>` prints it, and the release fails if it is empty).
3. Set the version in the `VERSION` file (for example `0.1`) and commit both files.
4. Tag the commit with the version itself (no `v` in front) and push the tag:
   ```sh
   git tag 0.1
   git push origin 0.1
   ```
5. The workflow **Release** then: checks that the tag equals the content of `VERSION` and that the changelog has the notes; runs **all** the test suites and
   the page tests (about 45 minutes); builds the payload (the import check is part of the build); and publishes the release with
   `Transmission-<version>.elf`, `Transmission-<version>.elf.sha256` and the notes.

The "Releases → latest" link of the README always points to the newest release. Releases are not marked as pre-releases on purpose: GitHub's "latest" ignores pre-releases.

**If the release fails** (a test failed, a typo in the tag): fix the problem on `main`, delete the tag (`git push origin :refs/tags/0.1` and `git tag -d 0.1`), delete the
half-made release on the Releases page if it exists, and tag again. Run the **Tests** workflow by hand ("Run workflow", "full") at any time to check the whole set without a release.

The `VERSION` of the development builds stays `0.0.N` until the first public release; after 0.1 raise it as usual and write a `CHANGELOG.md` section for each release.
