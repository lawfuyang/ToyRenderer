@echo off

ECHO Checking repository status...
git status

ECHO Pulling latest changes...
git fetch origin
git pull origin main

ECHO Updating submodules...
git submodule sync --recursive
git submodule update --init --recursive

ECHO Pruning remote branches...
git fetch --prune

ECHO Git operations completed.

pause
