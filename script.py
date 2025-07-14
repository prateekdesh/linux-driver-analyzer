import sys
import os

sys.path.append('code-evaluation-engine')
from quantitative import run_cppcheck
from qualitative import run_qualitative_analysis, parse_qualitative_score

def parse_quantitative_score(cppcheck_results):
    severity_weights = {
        'error': 10,
        'warning': 5,
        'style': 2,
        'performance': 3,
        'portability': 2,
        'information': 1
    }
    
    total_penalty = 0
    
    if not cppcheck_results or 'errors' not in cppcheck_results:
        return 100
    
    for error in cppcheck_results['errors']:
        severity = error.get('severity', 'unknown')
        
        if error.get('id') in ['checkersReport', 'missingIncludeSystem']:
            continue
            
        weight = severity_weights.get(severity, 1)
        total_penalty += weight
    
    return max(0, 100 - total_penalty)



def analyze_code(source_file):
    # Convert to absolute path if relative
    if not os.path.isabs(source_file):
        source_file = os.path.abspath(source_file)
    
    quantitative_results = run_cppcheck(source_file)
    qualitative_results = run_qualitative_analysis(source_file, "code-evaluation-engine/prompt.txt")
    
    quant_score = parse_quantitative_score(quantitative_results)
    print("Static analysis score (deterministic): ", quant_score)
    qual_score = parse_qualitative_score(qualitative_results)
    print("LLM-as-a-judge analysis (heuristic): ", qual_score)
    
    final_score = ((quant_score*0.2) + (qual_score*0.8))
    
    return final_score

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <source_file.c>")
        sys.exit(1)
    
    source_file = sys.argv[1]
    
    if not os.path.isabs(source_file):
        source_file = os.path.abspath(source_file)
    
    if not os.path.exists(source_file):
        print(f"File not found: {source_file}")
        sys.exit(1)
    
    score = analyze_code(source_file)

    print("Final Score: ",score)