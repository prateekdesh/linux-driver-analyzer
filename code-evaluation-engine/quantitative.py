import subprocess
import xml.etree.ElementTree as ET
import json

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
        
        if result.stderr:
            print("Cppcheck stderr (non-XML output, if any):\n", result.stderr)


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

    except subprocess.CalledProcessError as e:
        print(f"Error running Cppcheck: {e}")
        print(f"Stdout: {e.stdout}")
        print(f"Stderr: {e.stderr}")
        return None
    except FileNotFoundError:
        print("Error: cppcheck command not found. Make sure it's installed and in your PATH.")
        return None
    except ET.ParseError:
        print(f"Error parsing Cppcheck XML report from {output_xml}. Check if the file was generated correctly.")
        return None