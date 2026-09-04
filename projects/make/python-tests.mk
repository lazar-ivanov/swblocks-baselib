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
#   must be installed and in PATH

# Detect platform and set Python paths accordingly
ifeq (win, $(findstring win, $(OS)))
  # Windows: .venv/Scripts/python.exe
  PYTHON_VENV := .venv/Scripts/python.exe
  PYTHON_PIP := .venv/Scripts/pip.exe
else
  # Unix (macOS/Linux): .venv/bin/python
  PYTHON_VENV := .venv/bin/python
  PYTHON_PIP := .venv/bin/pip
endif

# Path to requirements file
PYTEST_REQUIREMENTS := scripts/tests/requirements-test.txt

# ========== Installation Target ==========
.PHONY: pytest-install
pytest-install:
	@echo "Setting up Python virtual environment..."
	@if [ ! -d .venv ]; then \
		echo "Creating virtual environment with $(PYTHON)..."; \
		$(PYTHON) -m venv .venv; \
		echo "Creating .venv/.gitignore..."; \
		echo '*' > .venv/.gitignore; \
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
		echo "  $(PYTHON) -m venv .venv"; \
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
	@cd scripts/tests && ../../$(PYTHON_VENV) -m pytest -v --cov --cov-report=term --cov-report=html
	@echo ""
	@echo "Coverage report generated in scripts/tests/htmlcov/"

.PHONY: pytest-unit
pytest-unit: pytest-check
	@echo "Running Stage 1: Unit tests..."
	@cd scripts/tests && ../../$(PYTHON_VENV) -m pytest -v test_bl_tool_unit.py --cov --cov-report=term

.PHONY: pytest-functional
pytest-functional: pytest-check
	@echo "Running Stage 2: Functional tests..."
	@cd scripts/tests && ../../$(PYTHON_VENV) -m pytest -v test_bl_tool_functional.py --cov --cov-report=term

.PHONY: pytest-fast
pytest-fast: pytest-check
	@echo "Running fast tests (small files only)..."
	@cd scripts/tests && ../../$(PYTHON_VENV) -m pytest -v -m "not slow"

.PHONY: pytest-coverage
pytest-coverage: pytest-check
	@echo "Generating detailed coverage report..."
	@cd scripts/tests && ../../$(PYTHON_VENV) -m pytest --cov --cov-report=html --cov-report=term-missing
	@echo "Coverage report: scripts/tests/htmlcov/index.html"
