import sys
import os

# Add the code-evaluation-engine directory to path
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'code-evaluation-engine'))

from qualitative import run_qualitative_analysis

def main():
    # Path to the C file to analyze (relative to this script)
    source_file = os.path.join(os.path.dirname(__file__), '..', 'code-evaluation-engine', 'bad_driver.c')
    source_file = os.path.abspath(source_file)
    
    if len(sys.argv) > 1:
        source_file = os.path.abspath(sys.argv[1])
    
    # Run qualitative analysis
    result = run_qualitative_analysis(source_file)
    
    if "error" in result:
        print(f"Error: {result['error']}")
        return None
    
    # Extract just the score for consistency with quantitative analysis
    score = result.get('overall_score')
    if score is not None:
        print(score)
    else:
        # If no score found, return a default or try to parse from analysis
        print("No score found in analysis")

if __name__ == "__main__":
    main()
