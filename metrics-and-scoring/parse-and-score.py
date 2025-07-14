import json
import sys
import os
import subprocess
import xml.etree.ElementTree as ET

def run_cppcheck(source_file_or_dir, output_xml="cppcheck_report.xml", enable_checks="all"):
    command = [
        "cppcheck",
        source_file_or_dir,
        f"--enable={enable_checks}",
        "--xml", 
        f"--output-file={output_xml}", 
        "-q"                  
    ]

    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        tree = ET.parse(output_xml)
        root = tree.getroot()

        errors = []
        for error_element in root.findall(".//error"):
            severity = error_element.get("severity")
            msg_id = error_element.get("id")
            message = error_element.get("msg")
            verbose_msg = error_element.get("verbose")  

            location = error_element.find("location")
            if location is not None:
                file_path = location.get("file")
                line_num = int(location.get("line", 0))
                column = int(location.get("column", 0))
            else:
                file_path, line_num, column = "N/A", 0, 0

            errors.append({
                "severity": severity,
                "id": msg_id,
                "message": message,
                "verbose_message": verbose_msg,
                "file": file_path,
                "line": line_num,
                "column": column
            })
        return {"errors": errors}

    except (subprocess.CalledProcessError, FileNotFoundError, ET.ParseError):
        return None

def parse_and_score_cppcheck_results(cppcheck_results):
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
    
    quality_score = max(0, 100 - total_penalty)
    return quality_score

def main():
    script_dir = os.path.dirname(__file__)
    source_file = os.path.join(script_dir, "..", "code-evaluation-engine", "bruh.c")
    source_file = os.path.abspath(source_file)
    
    if len(sys.argv) > 1:
        source_file = os.path.abspath(sys.argv[1])
    
    cppcheck_results = run_cppcheck(source_file)
    
    if cppcheck_results:
        score = parse_and_score_cppcheck_results(cppcheck_results)
        print(score)

if __name__ == "__main__":
    main()