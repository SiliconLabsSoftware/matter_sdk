#!/usr/bin/env python3
"""
Script to post SonarQube static analysis results to GitHub PR as a comment.
"""

import argparse
import json
import os
import requests
import sys
import re
from datetime import datetime

def post_pr_comment(github_token, repo_owner, repo_name, pr_number, comment_body):
    """Post a comment to GitHub PR"""
    url = f"https://api.github.com/repos/{repo_owner}/{repo_name}/issues/{pr_number}/comments"
    
    headers = {
        "Authorization": f"token {github_token}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/json"
    }
    
    payload = {"body": comment_body}
    
    response = requests.post(url, headers=headers, json=payload)
    
    if response.status_code == 201:
        print(f"✅ Successfully posted SonarQube results to PR #{pr_number}")
        return True
    else:
        print(f"❌ Failed to post comment to PR #{pr_number}")
        print(f"Status: {response.status_code}")
        print(f"Response: {response.text}")
        return False

def post_commit_status(github_token, repo_owner, repo_name, commit_sha, state, description, target_url):
    """Post commit status to GitHub"""
    url = f"https://api.github.com/repos/{repo_owner}/{repo_name}/statuses/{commit_sha}"
    
    headers = {
        "Authorization": f"token {github_token}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/json"
    }
    
    payload = {
        "state": state,
        "target_url": target_url,
        "description": description,
        "context": "ci/sonarqube-analysis"
    }
    
    response = requests.post(url, headers=headers, json=payload)
    
    if response.status_code == 201:
        print(f"✅ Successfully posted commit status for {commit_sha[:8]}")
        return True
    else:
        print(f"❌ Failed to post commit status for {commit_sha[:8]}")
        print(f"Status: {response.status_code}")
        print(f"Response: {response.text}")
        return False

def fetch_sonarqube_quality_gate(sonar_token, sonar_url, project_key, branch_name=None, pr_key=None):
    """Fetch detailed quality gate information from SonarQube API"""
    try:
        headers = {"Authorization": f"Bearer {sonar_token}"}
        
        # Build the API URL for quality gate status
        if pr_key:
            # For pull requests
            api_url = f"{sonar_url}/api/qualitygates/project_status?projectKey={project_key}&pullRequest={pr_key}"
        elif branch_name:
            # For branches
            import urllib.parse
            encoded_branch = urllib.parse.quote(branch_name, safe='')
            api_url = f"{sonar_url}/api/qualitygates/project_status?projectKey={project_key}&branch={encoded_branch}"
        else:
            # Main branch
            api_url = f"{sonar_url}/api/qualitygates/project_status?projectKey={project_key}"
        
        print(f"🔍 Fetching quality gate from: {api_url}")
        response = requests.get(api_url, headers=headers, timeout=30)
        
        if response.status_code == 200:
            data = response.json()
            return data.get('projectStatus', {})
        else:
            print(f"⚠️  Failed to fetch quality gate details: {response.status_code}")
            return None
            
    except Exception as e:
        print(f"⚠️  Error fetching quality gate details: {str(e)}")
        return None

def fetch_sonarqube_measures(sonar_token, sonar_url, project_key, branch_name=None, pr_key=None):
    """Fetch detailed measures from SonarQube API"""
    try:
        headers = {"Authorization": f"Bearer {sonar_token}"}
        
        # Metrics we want to fetch
        metrics = [
            "new_bugs", "new_vulnerabilities", "new_violations", "new_security_hotspots", 
            "new_code_smells", "new_coverage", "new_duplicated_lines_density",
            "new_maintainability_rating", "new_security_rating", "new_lines",
            "bugs", "vulnerabilities", "violations", "security_hotspots", "code_smells",
            "coverage", "duplicated_lines_density", "ncloc", "reliability_rating"
        ]
        
        metric_keys = ",".join(metrics)
        
        if pr_key:
            api_url = f"{sonar_url}/api/measures/component?component={project_key}&pullRequest={pr_key}&metricKeys={metric_keys}"
        elif branch_name:
            import urllib.parse
            encoded_branch = urllib.parse.quote(branch_name, safe='')
            api_url = f"{sonar_url}/api/measures/component?component={project_key}&branch={encoded_branch}&metricKeys={metric_keys}"
        else:
            api_url = f"{sonar_url}/api/measures/component?component={project_key}&metricKeys={metric_keys}"
        
        print(f"🔍 Fetching measures from: {api_url}")
        response = requests.get(api_url, headers=headers, timeout=30)
        
        if response.status_code == 200:
            data = response.json()
            measures = {}
            for measure in data.get('component', {}).get('measures', []):
                metric_name = measure['metric']
                # For "new_" metrics, the value is often in the period object
                if 'period' in measure and measure['period']:
                    value = measure['period'].get('value', '0')
                else:
                    value = measure.get('value', '0')
                measures[metric_name] = value
            return measures
        else:
            print(f"⚠️  Failed to fetch measures: {response.status_code}")
            return {}
            
    except Exception as e:
        print(f"⚠️  Error fetching measures: {str(e)}")
        return {}

def convert_rating_to_letter(rating_value):
    """Convert SonarQube numeric rating (1-5) to letter grade (A-E)"""
    if rating_value == 'N/A' or rating_value is None or rating_value == '':
        return 'N/A'
    
    try:
        rating_num = int(float(rating_value))
        rating_map = {1: 'A', 2: 'B', 3: 'C', 4: 'D', 5: 'E'}
        return rating_map.get(rating_num, f'Unknown({rating_value})')
    except (ValueError, TypeError):
        return f'Invalid({rating_value})'

def create_comment_body(status, commit_sha, branch_name, target_branch, sonar_output, quality_gate_details=None, measures=None):
    """Create formatted comment body for GitHub PR"""
    
    # Determine status emoji and text
    if status == "PASSED":
        result_emoji = "✅"
        result_text = "**PASSED**"
    else:
        result_emoji = "❌"
        result_text = "**FAILED**"
    
    # Use measures if available, otherwise show N/A
    default_metrics = {
        'new_lines': 'N/A',
        'new_duplicated_lines_density': 'N/A',
        'new_violations': 'N/A',
        'new_code_smells': 'N/A',
        'new_bugs': 'N/A',
        'new_vulnerabilities': 'N/A',
        'new_security_hotspots': 'N/A',
        'new_maintainability_rating': 'N/A',
        'reliability_rating': 'N/A',
        'new_security_rating': 'N/A'
    }
    
    # Merge measures with defaults
    metrics = default_metrics.copy()
    if measures:
        metrics.update(measures)
    
    # Get quality gate status
    quality_gate_status = quality_gate_details.get('status', status) if quality_gate_details else status
    
    # Truncate output if too long - show last 2000 characters for most relevant info
    max_output_length = 2000
    if len(sonar_output) > max_output_length:
        truncated_output = "[Output truncated - showing last 2000 characters]\n\n..." + sonar_output[-max_output_length:]
    else:
        truncated_output = sonar_output
    
    comment_body = f"""## 🔍 SonarQube Static Analysis Results

**Result:** {result_emoji} {result_text}  
**Quality Gate Status:** {status}  
**Commit SHA:** `{commit_sha}`

### 📊 Analysis Summary
- **Branch:** `{branch_name}`
- **Target:** `{target_branch}`
- **Analysis Time:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')}

### � Key Metrics
| Metric | Value |
|--------|-------|
| **New Lines of Code** | {metrics.get('new_lines', 'N/A')} |
| **New Duplicated Lines Density** | {metrics.get('new_duplicated_lines_density', metrics.get('duplicated_lines_density', 'N/A'))}% |
| **New Violations** | {metrics.get('new_violations', 'N/A')} |
| **New Code Smells** | {metrics.get('new_code_smells', 'N/A')} |
| **New Bugs** | {metrics.get('new_bugs', 'N/A')} |
| **New Vulnerabilities** | {metrics.get('new_vulnerabilities', 'N/A')} |
| **New Security Hotspots** | {metrics.get('new_security_hotspots', 'N/A')} |

### 🏆 Quality Ratings
| Category | Rating |
|----------|--------|
| **New Maintainability Rating** | {convert_rating_to_letter(metrics.get('new_maintainability_rating', 'N/A'))} |
| **Reliability Rating** | {convert_rating_to_letter(metrics.get('reliability_rating', 'N/A'))} |
| **New Security Rating** | {convert_rating_to_letter(metrics.get('new_security_rating', 'N/A'))} |

### �📋 Detailed Results
<details>
<summary>Click to view SonarQube output</summary>

```
{truncated_output}
```

</details>

---
*🤖 Automated comment by Jenkins CI*"""

    return comment_body

def main():
    parser = argparse.ArgumentParser(description="Post SonarQube results to GitHub PR")
    parser.add_argument("--github_token", required=True, help="GitHub access token")
    parser.add_argument("--repo_owner", default="SiliconLabsSoftware", help="GitHub repository owner")
    parser.add_argument("--repo_name", default="matter_sdk", help="GitHub repository name")
    parser.add_argument("--pr_number", required=True, help="Pull request number")
    parser.add_argument("--commit_sha", required=True, help="Git commit SHA")
    parser.add_argument("--status", required=True, help="SonarQube quality gate status")

    parser.add_argument("--branch_name", required=True, help="Source branch name")
    parser.add_argument("--target_branch", required=True, help="Target branch name")
    parser.add_argument("--sonar_output", help="SonarQube scanner output (deprecated - use --sonar_output_file)")
    parser.add_argument("--sonar_output_file", help="Path to file containing SonarQube scanner output")
    parser.add_argument("--sonar_token", help="SonarQube token for API access")
    parser.add_argument("--sonar_url", default="https://sonarqube.silabs.net", help="SonarQube server URL")
    parser.add_argument("--project_key", default="github_matter_sdk", help="SonarQube project key")
    
    args = parser.parse_args()
    
    try:
        # Read sonar output from file or use direct argument
        sonar_output = ""
        if args.sonar_output_file:
            try:
                with open(args.sonar_output_file, 'r') as f:
                    sonar_output = f.read()
                print(f"✅ Read SonarQube output from file: {args.sonar_output_file}")
            except Exception as e:
                print(f"❌ Error reading sonar output file {args.sonar_output_file}: {str(e)}")
                if args.sonar_output:
                    sonar_output = args.sonar_output
                    print("⚠️  Falling back to direct sonar_output argument")
                else:
                    raise
        elif args.sonar_output:
            sonar_output = args.sonar_output
            print("✅ Using direct sonar_output argument")
        else:
            raise ValueError("Either --sonar_output_file or --sonar_output must be provided")
        
        # Fetch detailed SonarQube information if token is provided
        quality_gate_details = None
        measures = None
        
        if args.sonar_token:
            print("🔍 Fetching detailed SonarQube quality gate information...")
            quality_gate_details = fetch_sonarqube_quality_gate(
                args.sonar_token, 
                args.sonar_url, 
                args.project_key, 
                args.branch_name, 
                pr_key=args.pr_number
            )
            
            print("📊 Fetching SonarQube measures...")
            measures = fetch_sonarqube_measures(
                args.sonar_token, 
                args.sonar_url, 
                args.project_key, 
                args.branch_name, 
                pr_key=args.pr_number
            )
        
        # Create comment body
        comment_body = create_comment_body(
            args.status,
            args.commit_sha,
            args.branch_name,
            args.target_branch,
            sonar_output,
            quality_gate_details,
            measures
        )
        
        # Post PR comment
        comment_success = post_pr_comment(
            args.github_token,
            args.repo_owner,
            args.repo_name,
            args.pr_number,
            comment_body
        )
        
        # Post PR comment only (skip commit status due to permissions)
        if comment_success:
            print("✅ GitHub PR comment posted successfully")
            sys.exit(0)
        else:
            print("❌ Failed to post GitHub PR comment")
            sys.exit(1)
            
    except Exception as e:
        print(f"❌ Error posting to GitHub: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main()