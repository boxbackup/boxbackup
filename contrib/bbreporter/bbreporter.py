#!/usr/bin/env python
# BoxBackupReporter - Simple script to report on backups that have been 
#                     performed using BoxBackup.
#
# Copyright: (C) 2007 Three A IT Limited
# Author: Kenny Millington <kenny.millington@3ait.co.uk>
#
# Credit: This script is based on the ideas of BoxReport.pl by Matt Brown of 
#         Three A IT Support Limited.
#
################################################################################
# !! Important !!
# To make use of this script you need to run the boxbackup client with the -v
# commandline option and set LogAllFileAccess = yes in your bbackupd.conf file.
# 
# Notes on lazy mode:
# If reporting on lazy mode backups you absolutely must ensure that
# logrotate (or similar) rotates the log files at the same rate at 
# which you run this reporting script or you will report on the same
# backup sessions on each execution.
#
# Notes on --rotate and log rotation in general: 
# The use-case for --rotate that I imagine is that you'll add a line like the 
# following into your syslog.conf file:-
# 
# local6.* -/var/log/box
# 
# Then specifying --rotate to this script will make it rotate the logs
# each time you report on the backup so that you don't risk a backup session 
# being spread across two log files (e.g. syslog and syslog.0). 
#
# NB: To do this you'll need to prevent logrotate/syslog from rotating your
#     /var/log/box file. On Debian based distros you'll need to edit two files.
#     
#     First: /etc/cron.daily/sysklogd, find the following line and make the 
#            the required change: 
#            Change: for LOG in `syslogd-listfiles`
#                To: for LOG in `syslogd-listfiles -s box`
#                                 
#     Second: /etc/cron.weekly/sysklogd, find the following line and make the 
#            the required change: 
#            Change: for LOG in `syslogd-listfiles --weekly`
#                To: for LOG in `syslogd-listfiles --weekly -s box`
#
# Alternatively, if suitable just ensure the backups stop before the 
# /etc/cron.daily/sysklogd file runs (usually 6:25am) and report on it
# before the files get rotated. (If going for this option I'd just use
# the main syslog file instead of creating a separate log file for box
# backup since you know for a fact the syslog will get rotated daily.)
#
################################################################################
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.  
#

# If sendmail is not in one of these paths, add the path.
SENDMAIL_PATHS = ["/usr/sbin/", "/usr/bin/", "/bin/" , "/sbin/"]

# The name of the sendmail binary, you probably won't need to change this.
SENDMAIL_BIN = "sendmail"

# Number of files to rotate around
ROTATE_COUNT = 7

# Import the required libraries
import sys, os, re, getopt, shutil, gzip

class BoxBackupReporter:
    class BoxBackupReporterError(Exception):
        pass
    
    def __init__(self, config_file="/etc/box/bbackupd.conf", 
                 log_file="/var/log/syslog", email_to=None, 
                 email_from="report@boxbackup", rotate=False, 
                 verbose=False, stats=False, sort=False, debug=False):
        
        # Config options
        self.config_file = config_file
        self.log_file = log_file
        self.email_to = email_to
        self.email_from = email_from
        self.rotate_log_file = rotate
        self.verbose_report = verbose
        self.usage_stats = stats
        self.sort_files = sort
        self.debug = debug

        # Regex's
        self.re_automatic_backup = re.compile(" *AutomaticBackup *= *no", re.I)
        self.re_syslog = re.compile("(\S+) +(\S+) +([\d:]+) +(\S+) +([^:]+): +"+
                                    "([^:]+): *(.*)") 
        
        # Initialise report
        self.reset()
        
    def _debug(self, msg):
        if self.debug:
            sys.stderr.write("[bbreporter.py Debug]: %s\n" % msg)

    def reset(self):
        # Reset report data to default values
        self.hostname = ""
        self.patched_files = []
        self.synced_files = []
        self.uploaded_files = []
        self.warnings = []
        self.errors = []
        self.stats = None
        self.start_datetime = "Unknown"
        self.end_datetime = "Unfinished"
        self.report = "No report generated"
    
    def run(self):
        try:
            self._determine_operating_mode()
            
            if self.lazy_mode:
                self._debug("Operating in LAZY MODE.")
            else:
                self._debug("Operating in SNAPSHOT MODE.")

        except IOError:
            raise BoxBackupReporter.BoxBackupReporterError("Error: "+\
                  "Config file \"%s\" could not be read." % self.config_file)
            
        try:
            self._parse_syslog()
        except IOError:
            raise BoxBackupReporter.BoxBackupReporterError("Error: "+\
                  "Log file \"%s\" could not be read." % self.log_file)
        
        self._parse_stats()
        self._generate_report()
        
    def deliver(self):
        # If we're not e-mailing the report then just dump it to stdout
        # and return.
        if self.email_to is None:
            print self.report
            # Now that we've delivered the report it's time to rotate the logs
            # if we're requested to do so.
            self._rotate_log()
            return
        
        # Locate the sendmail binary
        sendmail = self._locate_sendmail()
        if(sendmail is None):
            raise BoxBackupReporter.BoxBackupReporterError("Error: "+\
                  "Could not find sendmail binary - Unable to send e-mail!")
            
        
        # Set the subject based on whether we think we failed or not.
        # (suffice it to say I consider getting an error and backing up
        #  no files a failure or indeed not finding a start time in the logs).
        subject = "BoxBackup Reporter (%s) - " % self.hostname
        if self.start_datetime == "Unknown" or\
            (len(self.patched_files)  == 0 and len(self.synced_files) == 0 and\
             len(self.uploaded_files) == 0):
            subject = subject + "FAILED"
        else:
            subject = subject + "SUCCESS"

            if len(self.errors) > 0:
                subject = subject + " (with errors)"
            
        # Prepare the e-mail message.
        mail = []
        mail.append("To: " + self.email_to)
        mail.append("From: " + self.email_from)
        mail.append("Subject: " + subject)
        mail.append("")
        mail.append(self.report)
        
        # Send the mail.
        p = os.popen(sendmail + " -t", "w")
        p.write("\r\n".join(mail))
        p.close()
        
        # Now that we've delivered the report it's time to rotate the logs
        # if we're requested to do so.
        self._rotate_log()
         
    def _determine_operating_mode(self):
        # Scan the config file and determine if we're running in lazy or 
        # snapshot mode.
        cfh = open(self.config_file)

        for line in cfh:
            if not line.startswith("#"):
                if self.re_automatic_backup.match(line):
                    self.lazy_mode = False
                    cfh.close()
                    return
        
        self.lazy_mode = True
        cfh.close()
    
    def _parse_syslog(self):
        lfh = open(self.log_file)
        
        patched_files = {}
        uploaded_files = {}
        synced_files = {}
        
        for line in lfh:
            # Only run the regex if we find a box backup entry.
            if line.find("Box Backup") > -1 or line.find("bbackupd") > -1:
                raw_data = self.re_syslog.findall(line)
                try:
                    data = raw_data[0]
                except IndexError:
                    # If the regex didn't match it's not a message that we're
                    # interested in so move to the next line.
                    continue
                
                # Set the hostname, it shouldn't change in a log file
                self.hostname = data[3]
                
                # If we find the backup-start event then set the start_datetime.
                if data[6].find("backup-start") > -1:
                    # If we're not in lazy mode or the start_datetime hasn't
                    # been set then reset the data and set it.
                    #
                    # If we're in lazy mode and encounter a second backup-start
                    # we don't want to change the start_datetime likewise if 
                    # we're not in lazy mode we do want to and we want to reset
                    # so we only capture the most recent session.
                    if not self.lazy_mode or self.start_datetime == "Unknown":
                        self._debug("Reset start dtime with old time: %s." % 
                                    self.start_datetime)
                        
                        # Reset ourselves
                        self.reset()
                        
                        # Reset our temporary variables which we store
                        # the files in.
                        patched_files = {}
                        uploaded_files = {}
                        synced_files = {}

                        self.start_datetime = data[1]+" "+data[0]+ " "+data[2]
                        self._debug("Reset start dtime with new time %s." %
                                    self.start_datetime)
                                              
                # If we find the backup-finish event then set the end_datetime.
                elif data[6].find("backup-finish") > -1:
                    self.end_datetime = data[1] + " " + data[0] + " " + data[2]
                    self._debug("Set end dtime: %s" % self.end_datetime)
                
                # Only log the events if we have our start time.
                elif self.start_datetime != "Unknown":
                    # We found a patch event, add the file to the patched_files.
                    if data[5] == "Uploading patch to file":
                        patched_files[data[6]] = ""

                    # We found an upload event, add to uploaded files.
                    elif data[5] == "Uploading complete file":
                        uploaded_files[data[6]] = ""
                    
                    # We found another upload event.
                    elif data[5] == "Uploaded file":
                        uploaded_files[data[6]] = ""
                    
                    # We found a sync event, add the file to the synced_files.
                    elif data[5] == "Synchronised file":
                        synced_files[data[6]] = ""
                
                    # We found a warning, add the warning to the warnings.
                    elif data[5] == "WARNING":
                        self.warnings.append(data[6])

                    # We found an error, add the error to the errors.
                    elif data[5] == "ERROR":
                        self.errors.append(data[6])
        
        
        self.patched_files = patched_files.keys()
        self.uploaded_files = uploaded_files.keys()
        self.synced_files = synced_files.keys()
        
        # There's no point running the sort functions if we're not going
        # to display the resultant lists.
        if self.sort_files and self.verbose_report:
            self.patched_files.sort()
            self.uploaded_files.sort()
            
        
        lfh.close()
    
    def _parse_stats(self):
        if(not self.usage_stats):
            return
        
        # Grab the stats from bbackupquery
        sfh = os.popen("bbackupquery usage quit", "r")
        raw_stats = sfh.read()
        sfh.close()
        
        # Parse the stats 
        stats_re = re.compile("commands.[\n ]*\n(.*)\n+", re.S)
        stats = stats_re.findall(raw_stats)
        
        try:
            self.stats = stats[0]
        except IndexError:
            self.stats = "Unable to retrieve usage information."
            
    def _generate_report(self):
        if self.start_datetime == "Unknown":
            self.report = "No report data has been found."
            return
        
        total_files = len(self.patched_files) + len(self.uploaded_files)
        
        report = []
        report.append("--------------------------------------------------")
        report.append("Report Title  : Box Backup - Backup Statistics")
        report.append("Report Period : %s - %s" % (self.start_datetime, 
                                                   self.end_datetime))
        report.append("--------------------------------------------------")
        report.append("")
        report.append("This is your box backup report, in summary:")
        report.append("")
        report.append("%d file(s) have been backed up." % total_files)
        report.append("%d file(s) were uploaded." % len(self.uploaded_files))
        report.append("%d file(s) were patched." % len(self.patched_files))
        report.append("%d file(s) were synchronised." % len(self.synced_files))
        
        report.append("")
        report.append("%d warning(s) occurred." % len(self.warnings))
        report.append("%d error(s) occurred." % len(self.errors))
        report.append("")
        report.append("")
        
        # If we asked for the backup stats and they're available
        # show them.
        if(self.stats is not None and self.stats != ""):
            report.append("Your backup usage information follows:")
            report.append("")
            report.append(self.stats)
            report.append("")
            report.append("")
            
        # List the files if we've been asked for a verbose report.
        if(self.verbose_report):
            if len(self.uploaded_files) > 0:
                report.append("Uploaded Files (%d)" % len(self.uploaded_files))
                report.append("---------------------")
                for file in self.uploaded_files:
                    report.append(file)
                report.append("")
                report.append("")
            
            if len(self.patched_files) > 0:
                report.append("Patched Files (%d)" % len(self.patched_files))
                report.append("---------------------")
                for file in self.patched_files:
                    report.append(file)
                report.append("")
                report.append("")
    
        # Always output the warnings/errors.
        if len(self.warnings) > 0:
            report.append("Warnings (%d)" % len(self.warnings))
            report.append("---------------------")
            for warning in self.warnings:
                report.append(warning)
            report.append("")
            report.append("")
        
        if len(self.errors) > 0:
            report.append("Errors (%d)" % len(self.errors))
            report.append("---------------------")
            for error in self.errors:
                report.append(error)
            report.append("")
            report.append("")

        self.report = "\r\n".join(report)

    def _locate_sendmail(self):
        for path in SENDMAIL_PATHS:
            sendmail = os.path.join(path, SENDMAIL_BIN)
            if os.path.isfile(sendmail):
                return sendmail

        return None
    
    def _rotate_log(self):
        # If we're not configured to rotate then abort.
        if(not self.rotate_log_file):
            return
        
        # So we have these files to possibly account for while we process the 
        # rotation:- 
        # self.log_file, self.log_file.0, self.log_file.1.gz, self.log_file.2.gz
        # self.log_file.3.gz....self.log_file.(ROTATE_COUNT-1).gz
        #
        # Algorithm:-
        # * Delete last file.
        # * Work backwards moving 5->6, 4->5, 3->4, etc... but stop at .0
        # * For .0 move it to .1 then gzip it.
        # * Move self.log_file to .0
        # * Done.
        
        # If it exists, remove the oldest file.
        if(os.path.isfile(self.log_file + ".%d.gz" % (ROTATE_COUNT - 1))):
            os.unlink(self.log_file + ".%d.gz" % (ROTATE_COUNT - 1))    

        # Copy through the other gzipped log files.
        for i in range(ROTATE_COUNT - 1, 1, -1):
            src_file = self.log_file + ".%d.gz" % (i - 1)
            dst_file = self.log_file + ".%d.gz" % i
            
            # If the source file exists move/rename it.
            if(os.path.isfile(src_file)):
                shutil.move(src_file, dst_file)
        
        # Now we need to handle the .0 -> .1.gz case.
        if(os.path.isfile(self.log_file + ".0")):
            # Move .0 to .1
            shutil.move(self.log_file + ".0", self.log_file + ".1")
            
            # gzip the file.
            fh = open(self.log_file + ".1", "r")
            zfh = gzip.GzipFile(self.log_file + ".1.gz", "w")
            zfh.write(fh.read())
            zfh.flush()
            zfh.close()
            fh.close()
            
            # If gzip worked remove the original .1 file.
            if(os.path.isfile(self.log_file + ".1.gz")):
                os.unlink(self.log_file + ".1")
        
        # Finally move the current logfile to .0
        shutil.move(self.log_file, self.log_file + ".0") 


def stderr(text):
    sys.stderr.write("%s\n" % text)

def usage():
    stderr("Usage: %s [OPTIONS]\n" % sys.argv[0])
    stderr("Valid Options:-")
    stderr("  --logfile=LOGFILE\t\t\tSpecify the logfile to process,\n"+\
           "\t\t\t\t\tdefault: /var/log/syslog\n")
    
    stderr("  --configfile=CONFIGFILE\t\tSpecify the bbackupd config file,\n "+\
           "\t\t\t\t\tdefault: /etc/box/bbackupd.conf\n")
    
    stderr("  --email-to=user@example.com\t\tSpecify the e-mail address(es)\n"+\
           "\t\t\t\t\tto send the report to, default is to\n"+\
           "\t\t\t\t\tdisplay the report on the console.\n")
    
    stderr("  --email-from=user@example.com\t\tSpecify the e-mail address(es)"+\
           "\n\t\t\t\t\tto set the From: address to,\n "+\
           "\t\t\t\t\tdefault: report@boxbackup\n")
    
    stderr("  --stats\t\t\t\tIncludes the usage stats retrieved from \n"+\
           "\t\t\t\t\t'bbackupquery usage' in the report.\n")
    
    stderr("  --sort\t\t\t\tSorts the file lists in verbose mode.\n")
    
    stderr("  --debug\t\t\t\tEnables debug output.\n")

    stderr("  --verbose\t\t\t\tList every file that was backed up to\n"+\
           "\t\t\t\t\tthe server, default is to just display\n"+\
           "\t\t\t\t\tthe summary.\n")
    
    stderr("  --rotate\t\t\t\tRotates the log files like logrotate\n"+\
           "\t\t\t\t\twould, see the comments for a use-case.\n")

def main():
    # The defaults
    logfile = "/var/log/syslog"
    configfile = "/etc/box/bbackupd.conf"
    email_to = None
    email_from = "report@boxbackup"
    rotate = False
    verbose = False 
    stats = False
    sort = False
    debug = False
    # Parse the options
    try:
        opts, args = getopt.getopt(sys.argv[1:], "dosrvhl:c:t:f:", 
                        ["help", "logfile=", "configfile=","email-to=", 
                         "email-from=","rotate","verbose","stats","sort",
                         "debug"])
    except getopt.GetoptError:
        usage()
        return
    
    for opt, arg in opts:
        if(opt in ("--logfile","-l")):
            logfile = arg
        elif(opt in ("--configfile", "-c")):
            configfile = arg
        elif(opt in ("--email-to", "-t")):
            email_to = arg
        elif(opt in ("--email-from", "-f")):
            email_from = arg
        elif(opt in ("--rotate", "-r")):
            rotate = True
        elif(opt in ("--verbose", "-v")):
            verbose = True
        elif(opt in ("--stats", "-s")):
            stats = True
        elif(opt in ("--sort", "-o")):
            sort = True
        elif(opt in ("--debug", "-d")):
            debug = True
        elif(opt in ("--help", "-h")):
            usage()
            return
    
    # Run the reporter
    bbr = BoxBackupReporter(configfile, logfile, email_to, email_from, 
                            rotate, verbose, stats, sort, debug)
    try:
        bbr.run()
        bbr.deliver()
    except BoxBackupReporter.BoxBackupReporterError, error_msg:
        print error_msg

if __name__ == "__main__":
    main()
