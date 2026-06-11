# Security Statement

## System Security Hardening

You are advised to enable **address space layout randomization** (ASLR) level 2 in the system. Run the following command to enable it:

```sh
echo 2 > /proc/sys/kernel/randomize_va_space
```

## Running User Recommendations

1. All tools in this repository are designed for communication and interaction between tools such as msopprof and mssantizer, and do not provide any external functionality. You should not use the functions of this repository independently.

2. This tool is intended for development use only. It does not restrict ownership or permissions during installation, nor does it restrict the ownership or permissions of files processed by the tool. You are responsible for assigning appropriate ownership and permissions based on their specific usage scenarios, and must ensure that the content of files processed by the tool is secure and trustworthy.

3. When providing input to the tool, you must ensure that the content is secure and trustworthy, avoid using symbolic links, and convert any file to its real absolute path before passing it to the tool.

## File Permission Control

 1. By default, tool files written to the drive are not writable by others. You can manually control the permissions for the generated files as needed.

 2. Proper permission control is essential during installation and use. For details, see the following table.

### File Permission Reference

| Type                              | Maximum Linux Permission|
| ---------------------------------- | ------------------- |
| Home directory                        | 750 (rwxr-x---)   |
| Program files (including scripts and libraries)    | 550 (r-xr-x---)   |
| Program file directory                      | 550 (r-xr-x---)   |
| Configuration files                          | 640 (rw-r-----)   |
| Configuration file directory                      | 750 (rwxr-x---)   |
| Log files (recorded or archived)    | 440 (r--r-----)   |
| Log files (being recorded)                | 640 (rw-r-----)   |
| Log file directory                      | 750 (rwxr-x---)   |
| Debug files                         | 640 (rw-r-----)   |
| Debug file directory                     | 750 (rwxr-x---)   |
| Temporary file directory                      | 750 (rwxr-x---)   |
| Maintenance and upgrade file directory                  | 770 (rwxrwx---)   |
| Service data files                      | 640 (rw-r-----)   |
| Service data file directory                  | 750 (rwxr-x---)   |
| Key components, private keys, certificates, and ciphertext file directory| 700 (rwx------)   |
| Key components, private keys, certificates, and ciphertext files    | 600 (rw-------)   |
| APIs and scripts for encryption and decryption            | 500 (r-x------)   |

## Vulnerability Security Statement

For details, see [MindStudio Vulnerability Handling Procedure](./vulnerability_handling_procedure.md).

## Data Security

During the use of this tool, certain code-line-related features may access the user's operator code. If the operator code is confidential and must not be leaked, you should delete the corresponding deliverables promptly after use to prevent unnecessary information disclosure.

## Build Security

This tool supports source code compilation and installation. During compilation, third-party dependencies may be downloaded and build shell scripts executed, resulting in temporary program files and build directories. You may control permissions on files within the source code directory as needed to mitigate security risks. During the build process, you may also modify build scripts as necessary to avoid related security risks and ensure the security of build artifacts.

## Runtime Security

When runtime exceptions occur, the tool will exit the process and print error messages. This is expected behavior. You are advised to locate the specific cause of errors based on the error prompts, including checking log files and result files generated during profiling and parsing.

## Public Network Addresses

This tool does not involve the use of public IP addresses.

## Public APIs

This project is developed in C++ with source code released. You are advised to use the public APIs specified in the documentation. Directly calling source code APIs that are not explicitly disclosed is not recommended.

## Usage of Secure Functions

While unsecure functions are not forcibly disabled, you are advised to use their secure variants that explicitly take a buffer length parameter, for example, `memset_s` and `memcpy_s`.

## Communication Security Hardening

Remote communication is not involved. You are advised to use the tool in a secure network environment with a firewall or LAN, and pay attention to the communication security of other third-party software.

## Communication Matrix

External port communication is not involved.
