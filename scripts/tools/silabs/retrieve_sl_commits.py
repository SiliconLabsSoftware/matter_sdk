#! /usr/bin/python3
import argparse
import subprocess
from argparse import RawTextHelpFormatter

# field separator for the git log output
FIELD_SEP = "  |  "


def get_git_log(start_sha, end_sha, prefixes):
    try:
        # Run the git log command with output format <Commit short hash>  |  <Author>  |  <Title>
        result = subprocess.run(
            ['git', 'log', f'--pretty=format:%h{FIELD_SEP}%an{FIELD_SEP}%s', f'{start_sha}..{end_sha}'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            text=True
        )

        # Split the result into lines
        log_lines = result.stdout.splitlines()

        # Initialize a dictionary to hold commits by prefix
        commits_by_prefix = {prefix: [] for prefix in prefixes}

        # Filter and group commits based on the prefixes
        for line in log_lines:
            for prefix in prefixes:
                if prefix in line:
                    commits_by_prefix[prefix].append(line)
                    break

        return commits_by_prefix

    except subprocess.CalledProcessError as e:
        print(f"Error running git log: {e}")
        return {}


def get_commit_hash(commit_line):
    """Extract the commit hash from a formatted git log line."""
    return commit_line.split(FIELD_SEP, 1)[0]


def get_commit_subject(commit_line):
    """Extract the commit subject (title) from a formatted git log line."""
    parts = commit_line.split(FIELD_SEP, 2)
    return parts[2] if len(parts) == 3 else commit_line


def branch_contains_sha(sha, target_branch):
    """Return True if target_branch contains the exact commit sha.

    Uses `git branch --contains`, so this is True only when the very same
    commit (same SHA) is reachable from target_branch, not a cherry-picked copy.
    """
    try:
        result = subprocess.run(
            ['git', 'branch', '-a', '--contains', sha, '--format=%(refname:short)'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            text=True
        )
        branches = {line.strip() for line in result.stdout.splitlines() if line.strip()}
        return target_branch in branches
    except subprocess.CalledProcessError as e:
        print(f"Error checking branch --contains for '{sha}': {e.stderr.strip()}")
        return False


def is_subject_on_branch(subject, target_branch):
    """Return True if a commit with the given subject exists on target_branch.

    Used to detect cherry-picked copies, which carry a new SHA but keep the
    original title. --fixed-strings avoids interpreting the subject as a regex.
    """
    try:
        result = subprocess.run(
            ['git', 'log', target_branch, '--fixed-strings', f'--grep={subject}', '--pretty=format:%H'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            text=True
        )
        return bool(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print(f"Error checking branch '{target_branch}': {e.stderr.strip()}")
        return False


def filter_commit_presence(commit_line, target_branch, label):
    """Filter commits based on their presence on target_branch.

    - [PRESENT on <branch>]: the exact SHA is reachable from the branch.
    - [CHERRY-PICKED on <branch>]: the SHA is not on the branch, but a commit
      with the same title is (i.e. it was cherry-picked).
    - [MISSING from <branch>]: neither the SHA nor the title was found.
    """
    sha = get_commit_hash(commit_line)
    if branch_contains_sha(sha, target_branch):
        if label:
            print("%s%s[PRESENT on %s]" % (commit_line, FIELD_SEP, target_branch))
    elif is_subject_on_branch(get_commit_subject(commit_line), target_branch):
        if label:
            print("%s%s[CHERRY-PICKED on %s]" % (commit_line, FIELD_SEP, target_branch))
    else:
        if label:
            print("%s%s[MISSING from %s]" % (commit_line, FIELD_SEP, target_branch))
        else:
            print(commit_line)


def main():
    parser = argparse.ArgumentParser(formatter_class=RawTextHelpFormatter, description="""
    This script will parse git logs for our silabs prefixes ([SL-UP], [SL-TEMP], [SL-ONLY] or [CSA-CP]) between the commit SHAs provided in parameters
    on the current git branch.
    It will then output, per prefix, the commit sha and commit Title in the following format)
    [PREFIX] commits:
    <full_commit_sha> -- <Commit_Title>

    When --target-branch is provided, each identified commit is checked against that provided branch and if the commit is present on the branch the commit will be excluded from the output.
    if --label is also provided, then every commit will be outputted but with an additional annotation:
      [PRESENT on <branch>]       the exact SHA is reachable from the branch (git branch --contains).
      [CHERRY-PICKED on <branch>] the SHA is not on the branch, but a commit with the same title is.
      [MISSING from <branch>]     neither the SHA nor the title was found on the branch.
    """,
                                     epilog="""
    Post result developer actions:
       commits grouped under [SL-UP] shall be upstream the CSA master.
       commits grouped under [SL-ONLY] shall be cherry-picked to matter_sdk main branch.
       commits grouped under [SL-TEMP] must be revised. Are they still required, are they needed on main or for the next release. If they are, they need to be cherry-picked.
       commits grouped under [CSA-CP] are purely informative. They already exist in CSA master and will automatically be brought to main or the new release branch through CSA master merges.
    """)
    parser.add_argument('start_sha', type=str, help='The starting commit SHA')
    parser.add_argument('end_sha', type=str, help='The ending commit SHA')
    parser.add_argument('-t', '--target-branch', type=str, default=None,
                        help='Check whether each listed commit is already present on this branch\n'
                             '(e.g. main or origin/main). Distinguishes an exact SHA match (PRESENT)\n'
                             'from a same-title cherry-pick (CHERRY-PICKED). Make sure the branch is fetched locally.')
    parser.add_argument('-l', '--label', action='store_true', default=False,
                        help='Add Presence labels to the commits output else the present commit will be filtered out. Must be used in combination with --target-branch.')

    args = parser.parse_args()

    start_sha = args.start_sha
    end_sha = args.end_sha
    target_branch = args.target_branch
    label = args.label
    prefixes = ["[SL-UP]", "[SL-TEMP]", "[SL-ONLY]", "[CSA-CP]"]

    commits_by_prefix = get_git_log(start_sha, end_sha, prefixes)
    for prefix, commits in commits_by_prefix.items():
        print(f"{prefix} commits:")
        for commit in commits:
            # ONLY SL-ONLY and SL-TEMP could, potentially, already be present on the target branch
            if target_branch and prefix in ["[SL-ONLY]", "[SL-TEMP]"]:
                filter_commit_presence(commit, target_branch, label)
            else:
                print(commit)
        print()


if __name__ == "__main__":
    main()
