# Make PR Script

A Python utility for committing changes and creating GitHub pull requests via the command line.

## Prerequisites

* Git command line tools
* GitHub CLI (`gh`) installed and authenticated
* Python 3.6+

## Usage

This script automates the Git workflow for creating pull requests:
1. Creates/switches to a branch
2. Commits all changes
3. Pushes to the remote repository
4. Creates a pull request using GitHub CLI

### Basic Usage

```bash
./make_pr.py -b feature-branch -m "Your commit message"
```

### All Options

```bash
./make_pr.py --branch BRANCH_NAME \
             --commit-message "Your commit message" \
             --pr-title "Your PR title" \
             --pr-body "Your PR description" \
             --base-branch main
```

### Parameters

| Parameter | Short | Required | Description |
|-----------|-------|----------|-------------|
| `--branch` | `-b` | Yes | Branch name to use for the commit and PR |
| `--commit-message` | `-m` | Yes | Message for the Git commit |
| `--pr-title` | `-t` | No | Title for the PR (defaults to commit message) |
| `--pr-body` | `-d` | No | Description for the PR |
| `--base-branch` | | No | Base branch for the PR (default: main) |

## Examples

### Simple commit and PR

```bash
./make_pr.py -b bugfix-123 -m "Fix bug in authentication flow"
```

### Custom PR title and description

```bash
./make_pr.py -b feature-456 \
             -m "Add new reporting features" \
             -t "Feature: Enhanced reporting capabilities" \
             -d "This PR adds new reporting features including:

             - Daily summary reports
             - CSV export
             - Custom date range filtering"
```

### Target a different base branch

```bash
./make_pr.py -b hotfix-789 -m "Fix critical production bug" --base-branch production
