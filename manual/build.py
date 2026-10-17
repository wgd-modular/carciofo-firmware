#!/usr/bin/env python
#
# Builds the PDF manual. Every firmware in src/ gets its own chapter, taken
# from its README.md: the first `#` heading is the firmware's name, any text
# before the first `##` heading its introduction, and each `##` heading a
# section of the chapter. Who worked on a firmware and when it last changed
# comes from the git history of its folder. The result is written to
# manual/build/carciofo-manual.pdf.
#
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANUAL = ROOT / "manual"
OUT = MANUAL / "build"
REPO = os.environ.get("GITHUB_REPOSITORY", "wgd-modular/carciofo-firmware")

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")
# Firmwares for building and checking a module rather than playing it. They
# come last, after the instruments, like on the web flasher.
TOOLS = {"hardware-test"}

LOGINS = {}
NOREPLY = re.compile(r"^(?:\d+\+)?([^@]+)@users\.noreply\.github\.com$")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True, check=True).stdout


def parse_readme(text):
    """Splits a README into its title, introduction and `##` sections."""
    title, intro, sections = None, [], []
    fenced = False
    for line in text.splitlines():
        if FENCE.match(line):
            fenced = not fenced
        match = None if fenced else HEADING.match(line)
        if match and len(match.group(1)) == 1 and title is None:
            title = match.group(2)
        elif match and len(match.group(1)) == 2:
            sections.append({"title": match.group(2), "lines": []})
        elif sections:
            sections[-1]["lines"].append(line)
        else:
            intro.append(line)
    for section in sections:
        section["body"] = "\n".join(section.pop("lines")).strip()
    return title, "\n".join(intro).strip(), sections


def summary(intro, sections):
    """The first sentence of a firmware's description, as plain text."""
    text = intro or (sections[0]["body"] if sections else "")
    paragraph = next((block for block in text.split("\n\n")
                      if block.strip() and block.lstrip()[0] not in "|-*>!<"),
                     "")
    plain = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", paragraph)
    plain = re.sub(r"[*_`]", "", " ".join(plain.split()))
    match = re.match(r"(.+?[.!?:])(\s|$)", plain)
    sentence = match.group(1) if match else plain
    return sentence[:-1] + "." if sentence.endswith(":") else sentence


def github_api(path):
    request = urllib.request.Request("https://api.github.com/" + path,
                                     headers={"Accept":
                                              "application/vnd.github+json"})
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return json.load(response)
    except (OSError, ValueError):
        return None


def github_login(email, sha=None):
    """The GitHub account behind a commit email, if GitHub knows it."""
    noreply = NOREPLY.match(email)
    if noreply:
        return noreply.group(1)
    if sha:
        commit = github_api("repos/{}/commits/{}".format(REPO, sha)) or {}
        login = (commit.get("author") or {}).get("login")
        if login:
            return login
    # Co-authors are not linked on the commit, look for any commit of theirs.
    found = github_api("search/commits?per_page=1&q=author-email:" +
                       urllib.parse.quote(email)) or {}
    for item in found.get("items", []):
        return (item.get("author") or {}).get("login")
    return None


def history(folder):
    """Authors, first and last change of a folder, oldest author first."""
    log = git("log", "--reverse", "--format=%H%x1f%an%x1f%ae%x1f%as%x1f%s%x1f"
              "%(trailers:key=Co-authored-by,valueonly,separator=%x1e)%x1d",
              "--", str(folder.relative_to(ROOT)))
    entries = [entry.strip("\n").split("\x1f")
               for entry in log.split("\x1d") if entry.strip()]
    if not entries:
        return None

    people = {}
    for sha, name, email, date, subject, coauthors in entries:
        candidates = [(name, email)]
        for trailer in filter(None, coauthors.split("\x1e")):
            match = re.match(r"\s*(.*?)\s*<(.*)>", trailer)
            if match:
                candidates.append(match.groups())
        for person_name, person_email in candidates:
            if person_name.endswith("[bot]"):
                continue
            key = person_email.lower()
            if key not in people:
                people[key] = {"name": person_name, "email": key}
            if person_email == email:
                people[key].setdefault("sha", sha)

    # The same person often commits under more than one name or address,
    # merge them by GitHub account and keep the most complete name.
    authors = {}
    for person in people.values():
        if person["email"] not in LOGINS:
            LOGINS[person["email"]] = github_login(person["email"],
                                                   person.get("sha"))
        login = LOGINS[person["email"]]
        key = login or person["email"]
        name = person["name"]
        if key in authors:
            known = authors[key]["name"]
            name = known if " " in known or " " not in name else name
        authors[key] = {"name": name, "login": login}

    first, last = entries[0], entries[-1]
    return {
        "authors": list(authors.values()),
        "added": first[3],
        "updated": last[3],
        "commit": last[0][:7],
        "subject": last[4],
    }


def collect():
    firmwares = []
    readmes = sorted((ROOT / "src").glob("*/README.md"),
                     key=lambda path: (path.parent.name in TOOLS,
                                       path.parent.name))
    for readme in readmes:
        folder = readme.parent
        title, intro, sections = parse_readme(readme.read_text())
        firmwares.append({
            "id": folder.name,
            "title": title or folder.name,
            "intro": intro,
            "summary": summary(intro, sections),
            "sections": sections,
            "path": str(folder.relative_to(ROOT)),
            "tool": folder.name in TOOLS,
            "history": history(folder),
        })

    names = {}
    for firmware in firmwares:
        for author in (firmware["history"] or {}).get("authors", []):
            known = names.get(author["login"], "")
            if author["login"] and (" " in author["name"] or not known):
                names[author["login"]] = author["name"]
    for firmware in firmwares:
        for author in (firmware["history"] or {}).get("authors", []):
            author["name"] = names.get(author["login"], author["name"])

    manifest = json.loads((ROOT / ".release-please-manifest.json").read_text())
    head = git("log", "-1", "--format=%H%x1f%cs").strip().split("\x1f")
    version = manifest.get(".")
    tags = git("tag", "--points-at", "HEAD").split()
    return {
        "repo": REPO,
        "version": version,
        "release": "v{}".format(version) in tags,
        "commit": head[0][:7],
        "date": head[1],
        "built": datetime.date.today().isoformat(),
        "firmwares": firmwares,
    }


def main():
    OUT.mkdir(exist_ok=True)
    data = collect()
    (OUT / "data.json").write_text(json.dumps(data, indent=2,
                                              ensure_ascii=False))
    print("Collected {} firmwares".format(len(data["firmwares"])))
    pdf = OUT / "carciofo-manual.pdf"
    result = subprocess.call([
        "typst", "compile", "--root", str(ROOT),
        "--font-path", str(MANUAL / "fonts"), "--ignore-system-fonts",
        str(MANUAL / "manual.typ"), str(pdf),
    ])
    if result == 0:
        print("  -> {}".format(pdf.relative_to(ROOT)))
    sys.exit(result)


if __name__ == "__main__":
    main()
