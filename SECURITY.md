# Security policy

The latest release line receives security fixes. Do not disclose vulnerabilities through a public issue. Use GitHub's private vulnerability reporting for this repository when available, or contact the repository owner through their GitHub profile.

PokitSuite can write Arduino sketches and upload firmware when an MCP client explicitly invokes those tools. Review target paths, FQBN, port and edits. Keep the MCP server local; it uses stdio and is not designed as an exposed network service.

