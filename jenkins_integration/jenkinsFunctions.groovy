/**
 * Send SonarQube results to GitHub PR using Python script
 */
def send_sonar_results_to_github(commit_sha, result, status, sonar_output, pr_number, branch_name, target_branch) {
    withCredentials([
        usernamePassword(credentialsId: 'Matter-Extension-GitHub', usernameVariable: 'GITHUB_APP', passwordVariable: 'GITHUB_ACCESS_TOKEN')
    ]) {
        // Write sonar output to a temporary file to avoid "Argument list too long" error
        def tempFile = "${env.WORKSPACE}/sonar_output_${BUILD_NUMBER}.txt"
        writeFile file: tempFile, text: sonar_output
        
        try {
            sh """
                python3 -u jenkins_integration/github/send_sonar_results_to_github.py \\
                    --github_token \${GITHUB_ACCESS_TOKEN} \\
                    --repo_owner "SiliconLabsSoftware" \\
                    --repo_name "matter_sdk" \\
                    --pr_number ${pr_number} \\
                    --commit_sha ${commit_sha} \\
                    --result ${result} \\
                    --status ${status} \\
                    --branch_name "${branch_name}" \\
                    --target_branch "${target_branch}" \\
                    --sonar_output_file "${tempFile}"
            """
        } finally {
            // Clean up temporary file
            sh "rm -f '${tempFile}'"
        }
    }
}


/**
 * Publishes static analysis results to SonarQube.
 */
def publishSonarAnalysis() {

        // Use the SonarQube environment defined in Jenkins
        withSonarQubeEnv('Silabs SonarQube') {
        
        // Use credentials stored in Jenkins
        withCredentials([string(credentialsId: 'sonarqube_token', variable: 'SONAR_SECRET')]) {

            // Create necessary directories
            sh "mkdir -p ${env.WORKSPACE}/sonar"
            sh "mkdir -p ${env.WORKSPACE}/sonar-cache"
            sh "mkdir -p ${env.WORKSPACE}/sonar-user-home"

            // Prepare global SonarQube parameters
            def sonarqubeParams = [
                "-Dsonar.projectKey=matter_sdk",
                "-Dsonar.projectBaseDir=${env.WORKSPACE}",
                "-Dsonar.working.directory=${env.WORKSPACE}/sonar",
                "-Dsonar.token=${SONAR_SECRET}",
                "-Dsonar.cfamily.cache.enabled=true",
                "-Dsonar.cfamily.cache.path=${env.WORKSPACE}/sonar-cache",
                "-Dsonar.userHome=${env.WORKSPACE}/sonar-user-home",
                "-Duser.home=${env.WORKSPACE}/sonar-user-home",
                "-Dsonar.qualitygate.wait=true",
                "-Dsonar.cfamily.threads=32",
                "-Dsonar.sourceEncoding=UTF-8",
                "-Dsonar.sources=.",
                "-Dsonar.inclusions=**/*.c,**/*.h,**/*.cpp,**/*.hpp",
                "-Dsonar.exclusions=third_party/**"
            ]

            // Handle pull request analysis if applicable
            if (env.CHANGE_ID) {
                sonarqubeParams += [
                    "-Dsonar.pullrequest.key=${env.CHANGE_ID}",
                    "-Dsonar.pullrequest.branch=${env.CHANGE_BRANCH}",
                    "-Dsonar.pullrequest.base=${env.CHANGE_TARGET}"
                ]
            } else {
                sonarqubeParams += ["-Dsonar.branch.name=${env.BRANCH_NAME}"]
            }

            // Capture the sonar-scanner output with error handling
            def sonarOutput = ""
            def qualityGateResult = "FAIL"
            def qualityGateStatus = "FAILED"
            def commit_sha = env.GIT_COMMIT ?: "unknown"

            try {
                sonarOutput = sh(script: "sonar-scanner ${sonarqubeParams.join(' ')}", returnStdout: true).trim()
                echo "SonarQube Scanner Output:\n${sonarOutput}"

                // Parse quality gate status from output
                def qualityGateMatcher = sonarOutput =~ /QUALITY GATE STATUS:\s*(PASSED|FAILED)/
                if (qualityGateMatcher.find()) {
                    qualityGateStatus = qualityGateMatcher[0][1]
                    qualityGateResult = (qualityGateStatus == "PASSED") ? "PASS" : "FAIL"
                } else {
                    qualityGateResult = "PASS"
                }

                // Parse SCM revision ID from output (with fallback)
                def scmRevisionMatcher = sonarOutput =~ /SCM revision ID '([a-fA-F0-9]+)'/
                if (scmRevisionMatcher.find()) {
                    commit_sha = scmRevisionMatcher[0][1]
                    echo "Extracted SCM revision ID: ${commit_sha}"
                } else {
                    echo "SCM revision ID not found, using fallback: ${commit_sha}"
                }

            } catch (Exception e) {
                echo "SonarQube scanner failed with error: ${e.getMessage()}"
                sonarOutput = "SonarQube analysis failed: ${e.getMessage()}"
                qualityGateResult = "FAIL"
                qualityGateStatus = "FAILED"
            }

            echo "Static Analysis Quality Gate Status: ${qualityGateStatus}"
            echo "Static Analysis Result: ${qualityGateResult}"

            return [status: qualityGateStatus, result: qualityGateResult, output: sonarOutput, commit_sha: commit_sha]
        }
    }
}

/**
 * Take a Jenkins action (closure) such as node(){} and retry it in the event
 * of an exception where we think the node was reclaimed by AWS or otherwise
 * crashed
 */
def actionWithRetry(Closure action)
{
    def retryCount = 0
	def abortStepTime = 2
    timeout(time: 2, unit: 'HOURS')
    {
	    while(retryCount <= 5)
	    {
	        try
	        {
	            timeout(time: abortStepTime, unit: 'HOURS') {
					action.call()
				}
	            return
	        }
			//catch(org.jenkinsci.plugins.workflow.steps.FlowInterruptedException abort){ throw abort } //Throw this error if it is a Jenkins abort
			catch(Throwable ex)
	        {

	            def totalError = "Abort information: " + determineIfAbortOrTimeout(ex) + "\nOriginal errors: " + "\n" + ex.toString() + "\n" + "Full stack trace: " + "\n"+ "\n" + ex.getStackTrace().toString()

	            echo 'action threw exception at ' + java.time.LocalDateTime.now() + "\n" + "\n" + totalError

				//Treat null exceptions as ChannelClosedException, due to issues with hanging exceptions that dont return text with ChannelClosedException
				if(totalError.contains("Full stack trace: null") && !totalError.contains("hudson.AbortException: script returned exit code"))
					totalError += "\nNull exception detected, treating as AWS ChannelClosedException"


	            //if(totalError.contains('script returned exit code 2'))
	            //{
	            //	sendDevopsDebugMessage("SUDS failure detected, freezing worker")
	            //	input "Frozen for devops analysis"
	            //}
				if(totalError.contains('Aborted by'))
					throw ex
	            else if((!totalError.contains('ClosedChannelException')           &&
	                !totalError.contains('ChannelClosedException')                &&
					!totalError.contains('Unexpected termination of the channel') &&
					!totalError.contains('FlowInterruptedException')              &&
	                !totalError.contains('RemovedNodeListener')                   &&
	                !totalError.contains('missing workspace')                     &&
	                !totalError.contains('Unable to create live FilePath')        &&
					!totalError.contains('StringIndexOutOfBoundsException')       &&
	                !totalError.contains('MissingContextVariableException') )     ||
	                retryCount == 5)
	            {
					if(retryCount == 5)
	            	{
	            		echo "Retry count limit reached for AWS issues, throwing exception"
	                	throw ex
	                }
	                else
	                {
	                	//Print uc log if core dump detected. Don't fail if log doesn't exist for some reason
	                	//This did not work as the node has been left at this pointp

                      	//if(totalError.contains("script returned exit code 139"))
	                	//{
	                	//	sh "cat /home/buildengineer/.uc/uc.core.log 2>/dev/null"
	                	//}

	                	echo "No AWS errors found, throwing exception"
	                	throw ex
	                }
	            }

	            echo 'Lost slave connection. Retrying with count ' + retryCount
                sleep 90
	            retryCount++
	        }
	    }
	}
}
return this