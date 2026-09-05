# ========== Python Testing ==========
# Python testing targets for scripts/bl_tool.py
#
# Targets:
#   pytest-install  - Create .venv and install test dependencies
#   pytest-check    - Verify .venv and pytest availability
#   pytest          - Run all tests with coverage + HTML report
#   pytest-unit     - Run unit tests only
#   pytest-functional - Run functional tests only
#   pytest-fast     - Run fast tests (small files only)
#   pytest-coverage - Detailed coverage report with missing lines
#
# Prerequisites:
#   the interpreter named by $(PYTHON) (python3 on Linux/macOS, python on Windows)
#   must be installed and in PATH; it must be Python 3.6 or later (the devenv2-5
#   distributions provide Python 2.7, which has no venv module - pass PYTHON=<interpreter>
#   to override it, as documented for Windows in scripts/devenv7/AGENTS.md)

# Every path is anchored on the repository root (TOPDIR / TOPDIRABS, defined in the root
# Makefile) rather than on the current directory, like the rest of the makefiles
PYTHON_VENV_DIR := $(TOPDIRABS)/.venv

# Detect platform and set Python paths accordingly
ifeq (win, $(findstring win, $(OS)))
  # Windows: .venv/Scripts/python.exe
  PYTHON_VENV := $(PYTHON_VENV_DIR)/Scripts/python.exe
  PYTHON_PIP := $(PYTHON_VENV_DIR)/Scripts/pip.exe
else
  # Unix (macOS/Linux): .venv/bin/python
  PYTHON_VENV := $(PYTHON_VENV_DIR)/bin/python
  PYTHON_PIP := $(PYTHON_VENV_DIR)/bin/pip
endif

# Test directory and path to requirements file
PYTEST_DIR := $(TOPDIR)scripts/tests
PYTEST_REQUIREMENTS := $(PYTEST_DIR)/requirements-test.txt

# ========== Installation Target ==========
.PHONY: pytest-install
pytest-install:
	@echo "Setting up Python virtual environment..."
	@$(PYTHON) -c 'import sys; sys.exit(0 if sys.version_info >= (3, 6) else 1)' 2>/dev/null || \
		(echo "ERROR: pytest-install requires Python 3.6 or later, but $(PYTHON) is older or not available" && \
		 echo "       (the devenv2-5 distributions provide Python 2.7); pass PYTHON=<interpreter> to override" && \
		 exit 1)
	@if [ ! -d "$(PYTHON_VENV_DIR)" ]; then \
		echo "Creating virtual environment with $(PYTHON)..."; \
		$(PYTHON) -m venv "$(PYTHON_VENV_DIR)"; \
		echo "Creating .venv/.gitignore..."; \
		echo '*' > "$(PYTHON_VENV_DIR)/.gitignore"; \
	else \
		echo "Virtual environment .venv already exists, reinstalling dependencies..."; \
	fi
	@echo "Installing/updating test dependencies..."
	@$(PYTHON_PIP) install -r $(PYTEST_REQUIREMENTS)
	@echo ""
	@echo "Virtual environment ready at .venv/"
	@echo "  Python: $(PYTHON_VENV)"
	@echo "  Pytest: $(PYTHON_VENV) -m pytest"

# ========== Verification Target ==========
.PHONY: pytest-check
pytest-check:
	@if [ ! -f "$(PYTHON_VENV)" ]; then \
		echo "ERROR: Python virtual environment not found at $(PYTHON_VENV)"; \
		echo ""; \
		echo "To create the virtual environment, run:"; \
		echo "  make pytest-install"; \
		echo ""; \
		echo "Or manually:"; \
		echo "  $(PYTHON) -m venv $(PYTHON_VENV_DIR)"; \
		echo "  $(PYTHON_PIP) install -r $(PYTEST_REQUIREMENTS)"; \
		exit 1; \
	fi
	@$(PYTHON_VENV) -c "import pytest" 2>/dev/null || \
		(echo "ERROR: pytest not found in virtual environment" && \
		 echo "" && \
		 echo "To install test dependencies, run:" && \
		 echo "  make pytest-install" && \
		 exit 1)

# ========== Test Execution Targets ==========
.PHONY: pytest
pytest: pytest-check
	@echo "Running Python tests for bl_tool.py..."
	@cd $(PYTEST_DIR) && $(PYTHON_VENV) -m pytest -v --cov --cov-report=term --cov-report=html
	@echo ""
	@echo "Coverage report generated in scripts/tests/htmlcov/"

.PHONY: pytest-unit
pytest-unit: pytest-check
	@echo "Running Stage 1: Unit tests..."
	@cd $(PYTEST_DIR) && $(PYTHON_VENV) -m pytest -v test_bl_tool_unit.py --cov --cov-report=term

.PHONY: pytest-functional
pytest-functional: pytest-check
	@echo "Running Stage 2: Functional tests..."
	@cd $(PYTEST_DIR) && $(PYTHON_VENV) -m pytest -v test_bl_tool_functional.py --cov --cov-report=term

.PHONY: pytest-fast
pytest-fast: pytest-check
	@echo "Running fast tests (small files only)..."
	@cd $(PYTEST_DIR) && $(PYTHON_VENV) -m pytest -v -m "not slow"

.PHONY: pytest-coverage
pytest-coverage: pytest-check
	@echo "Generating detailed coverage report..."
	@cd $(PYTEST_DIR) && $(PYTHON_VENV) -m pytest --cov --cov-report=html --cov-report=term-missing
	@echo "Coverage report: scripts/tests/htmlcov/index.html"
