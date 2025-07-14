from google import genai
import re


def run_qualitative_analysis(source_file_path, prompt_template_path):
    with open(source_file_path, 'r') as file:
        source_code = file.read()
    with open(prompt_template_path, 'r') as f:
        prompt_template = f.read()
    prompt = prompt_template.replace('{source_code}', source_code)
    client = genai.Client(api_key="AIzaSyA6viwZuyjHhrc38a5TXh0uJvsJ2zFtGDs")
    response = client.models.generate_content(
        model="gemini-2.5-flash",
        contents=prompt,
    )
    return response.text


def parse_qualitative_score(analysis_text):
    score_patterns = [
        r'(?:score|rating)[:\s]*(\d+)(?:/100)?',
        r'(\d+)/100',
        r'(\d+)%'
    ]

    for pattern in score_patterns:
        match = re.search(pattern, analysis_text, re.IGNORECASE)
        if match:
            score = int(match.group(1))
            if 0 <= score <= 100:
                return score

    return 50
