
# C Code Analyzer: Static Analysis & LLM-as-a-judge based Evaluation System

  

## Overview

This project provides a robust framework for evaluating C source code (especially Linux kernel drivers) using both static analysis (Cppcheck) and qualitative LLM-based analysis (Google Gemini). The system combines deterministic and heuristic scoring, dynamically weighting results to produce a final score out of **100**.

  

## Features

-  **Static Analysis**: Uses Cppcheck to detect errors, warnings, style, performance, and portability issues in C code.

-  **Qualitative Analysis**: Uses Google Gemini LLM to audit code against a professional rubric, with a custom prompt template.

-  **Dynamic Weighting**: Final score is a weighted combination of static and LLM scores, with weights adjusted based on score difference between both the analyses.

-  **Extensible**: Modular Python scripts for easy extension and integration.

  

## Directory Structure

```

code-evaluation-engine/

bad_driver.c, good_driver.c, mid_driver.c # Example C files

prompt.txt # LLM prompt template

qualitative.py # LLM analysis logic

quantitative.py # Cppcheck analysis logic

metrics-and-scoring/

parse-and-score.py # Standalone Cppcheck scoring

qualitative-score.py # Standalone LLM scoring

script.py # Main orchestration script

.env # Gemini API key

```

  

## How It Works

### 1. Static Analysis (Cppcheck)

-  `quantitative.py` runs Cppcheck on the input C file.

- Parses XML output to extract errors and their severities.

- Applies severity weights to compute a quantitative score (out of 100).

- Severity Weights:
		- error:  10
		- warning:  5
		- style:  2
		- performance:  3
		- portability:  2
		- information:  1

  

### 2. Qualitative Analysis (LLM)

-  `qualitative.py` loads the C file and injects it into a detailed prompt from `prompt.txt`.

- Sends the prompt to Gemini via API key from `.env`.

- Parses the LLM's response to extract a score (out of 100).

- The prompt for the LLM involves an internally segmented rubrics:

		- Correctness (40% Weight): Checks if the code compiles, works as intended, and properly uses kernel APIs and conventions.
		- Security & Safety (25% Weight): Focuses on preventing vulnerabilities: secure user-kernel data transfer, proper resource cleanup, protection against race conditions, and valid input handling.
		- Code Quality (20% Weight): Evaluates adherence to kernel coding style, robust error handling, clear documentation, and maintainable structure.
		- Performance (10% Weight): Assesses algorithmic efficiency (avoiding slowdowns like locking too long) and responsible memory usage.
		- Advanced Features (5% Weight): Looks for the use of modern kernel practices (e.g., Device Tree, power management) and integrated debuggability.

  

### 3. Dynamic Weighting & Final Score

-  `script.py` orchestrates both analyses.

- If the difference between static and LLM scores is >30, weights are 20% static / 80% LLM.

- If the difference is ≤30, weights are 30% static / 70% LLM.

- Prints all intermediate and final scores for transparency.

  

## Usage

### Prerequisites

- Python 3.8+

- Cppcheck installed and available in your PATH

- Google Gemini API key (add to `.env` as `api_key=...`)

- Required Python packages (see below)

  

### Install Requirements

```bash

pip install  -r  requirements.txt

```

  

### Run Analysis

```bash

python script.py  <source_file.c>

```

- Example: `python script.py code-evaluation-engine/good_driver.c`

  

### Output

- Prints static score, LLM score, weights used, and final score.

  

## File Descriptions

-  **script.py**: Main entry point. Handles argument parsing, runs both analyses, applies dynamic weighting, and prints results.

-  **code-evaluation-engine/quantitative.py**: Runs Cppcheck, parses XML, computes static score.

-  **code-evaluation-engine/qualitative.py**: Loads prompt, sends to Gemini, parses LLM score.

-  **code-evaluation-engine/prompt.txt**: Detailed rubric for LLM analysis. `{source_code}` placeholder is replaced with actual code.

-  **metrics-and-scoring/parse-and-score.py**: Standalone Cppcheck runner and scorer.

-  **metrics-and-scoring/qualitative-score.py**: Standalone LLM runner and scorer.

-  **.env**: Stores Gemini API key (never commit this file!).

  

## Error Handling

- Handles missing files, API failures, and Cppcheck errors gracefully.

- Prints helpful error messages for debugging.

  

## Extending & Customizing

- You can add more C files to `code-evaluation-engine/` for testing.

- Modify `prompt.txt` to change the LLM rubric or instructions.

- Extend `quantitative.py` or `qualitative.py` for more metrics or different LLMs.

  

## Example Workflow

1. Place your C file in the project directory.

2. Run `python script.py <path_to_your_file.c>`.

3. Review the printed scores and weighting logic.

4. Use standalone scripts in `metrics-and-scoring/` for deeper analysis if needed.

  

## Requirements

- See `requirements.txt` for Python dependencies (e.g., `google-genai`, `python-dotenv`).

- Cppcheck must be installed separately.

  

## Security Notes

- Do not commit your `.env` file or API keys.

- All analysis is local except for LLM calls to Gemini.
