#!/usr/bin/env python3
"""
Script to commit changes and create a GitHub pull request using gh CLI.
"""

import argparse
import subprocess
import sys
import os


def run_command(cmd, check=True):
    """Run a shell command and return the output."""
    try:
        result = subprocess.run(
            cmd,
            check=check,
            text=True,
            capture_output=True,
            shell=isinstance(cmd, str)
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {cmd}")
        print(f"Error message: {e.stderr.strip()}")
        if check:
            sys.exit(1)
        return e.stderr.strip()


def check_for_changes():
    """Check if there are any changes in the git repository."""
    status = run_command(["git", "status", "--porcelain"])
    return len(status) > 0


def create_branch(branch_name):
    """Create and switch to a new git branch."""
    # Check if branch exists
    branches = run_command(["git", "branch", "--list", branch_name])

    if branches:
        # Branch exists, just switch to it
        run_command(["git", "checkout", branch_name])
        print(f"Switched to existing branch '{branch_name}'")
    else:
        # Create and switch to new branch
        run_command(["git", "checkout", "-b", branch_name])
        print(f"Created and switched to new branch '{branch_name}'")


def commit_changes(commit_message):
    """Stage and commit all changes."""
    if not check_for_changes():
        print("No changes to commit")
        return False

    # Stage all changes
    run_command(["git", "add", "."])

    # Commit changes
    run_command(["git", "commit", "-m", commit_message])
    print(f"Changes committed with message: '{commit_message}'")
    return True


def push_changes(branch_name):
    """Push changes to the remote repository."""
    result = run_command(
        ["git", "push", "--set-upstream", "origin", branch_name])
    print(f"Pushed changes to branch '{branch_name}'")
    return result


def create_pull_request(title, body=None, base="main"):
    """Create a pull request using gh CLI."""
    cmd = ["gh", "pr", "create", "--title", title]

    if body:
        cmd.extend(["--body", body])

    cmd.extend(["--base", base])

    result = run_command(cmd)
    print(f"Pull request created: {result}")
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Commit changes and create a PR')
    # parser.add_argument('--branch', '-b', required=True,
    #                     help='Branch name to use')
    # parser.add_argument('--commit-message', '-m',
    #                     required=True, help='Commit message')
    parser.add_argument(
        '--pr-title', '-t', help='PR title (defaults to commit message if not provided)')
    parser.add_argument('--pr-body', '-d', help='PR description')
    parser.add_argument('--base-branch', default="main",
                        help='Base branch for the PR (default: main)')

    args = parser.parse_args()
    branch_name = "remove-windows-2019"

    # Set PR title to commit message if not provided
    # pr_title = args.pr_title if args.pr_title else args.commit_message
    commit_message = "remove windows-2019"
    pr_body = "- Windows 2019 will be fully unsupported by 2025-06-30, https://github.com/actions/runner-images/issues/12045. Remove it with msvc-15 and below.\n \
                - Use latest windows 2025 and msvc-17\n\
                - Latest submodules"
    pr_title = "remove windows-2019 and latest submodules"

    # Create/switch to branch
    create_branch(branch_name)

    # Commit changes
    committed = commit_changes(commit_message)

    if committed:
        # Push changes
        push_changes(branch_name)

        # Create PR
        create_pull_request(pr_title, pr_body)
    else:
        print("No changes to commit, skipping PR creation")


if __name__ == "__main__":
    main()
