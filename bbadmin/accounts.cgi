#!/usr/bin/perl

# Box Backup web management interface (c) Chris Wilson, 2008
#
# LICENSE: The Box Backup license applies to this code, with the following
# additional conditions:
#
# If you make any changes to this code, except for changes to existing
# variables in the Configuration section below, you must publish the changes
# under the same license, whether or not you distribute copies of the 
# changed version.
#
# If you use any of this code in a derivative work, you must publish the
# source code of the derivative work under the same or compatible license,
# whether or not you distribute copies of the derivative work.
#
# The terms of the Box Backup license may be viewed here:
# https://www.boxbackup.org/license.html
#
# If you require access to the code under a different license, this may
# be negotiated with the copyright holder.

use strict;
use warnings;

# Variables which you may need to change to match your installation
# Changes to existing variables are NOT required to be published.

my $box_dir = "/etc/box";
my $bbstored_dir = "$box_dir/bbstored";
my $ca_dir = "/mnt/backup/boxbackup/ca";

# You should not need to change these unless you have a non-standard installation

my $bbstored_conf_file = "$box_dir/bbstored.conf";
my $bbstoreaccounts = "/usr/local/sbin/bbstoreaccounts";
my $accounts_db_file = undef;
# my $accounts_db_file = "/etc/box/bbstored/accounts.txt";
my $raidfile_conf_file = undef;
# my $raidfile_conf_file = "/etc/box/raidfile.conf";
my $sign_period = '5000';

# install Perl module with:
# perl -MCPAN -e 'install Config::Scoped'
# perl -MCPAN -e 'force install P/PT/PTHOMSEN/BoxBackup/BBConfig-0.03.tar.gz'
# perl -MCPAN -e 'install Convert::ASN1'
# download http://search.cpan.org/CPAN/authors/id/L/LE/LEO/Convert-X509-0.1.tar.gz,
# unpack, and move the Convert folder to /usr/lib/perl5/site_perl/X.X.X

# Check that SSL is being used.
# DO NOT DISABLE THIS unless you really know what you're doing!
die "This script requires an SSL web server" unless $ENV{HTTPS};

# Check that the script is protected by basic authentication.
# DO NOT DISABLE THIS unless you really know what you're doing!
die "This script requires HTTP Authentication" unless $ENV{REMOTE_USER};

# You should not need to change anything below this line.
# If you do, you must publish your changes to comply with the license.

use BoxBackup::Config::Accounts;
use BoxBackup::Config::DiskSets;
use CGI::Carp qw(fatalsToBrowser);
use CGI::Pretty;
use Config::Scoped;
use Convert::X509::Request;
use English;
use Fcntl;
use File::Temp;
use URI;
use URI::QueryParam;

sub check_access($$)
{
	my ($file,$desc) = @_;
	unless (-r $file)
	{
		open FILE, "< $file" and die "should have failed";
		die "Failed to access $desc ($file): $!";
	}
}

sub check_executable($$)
{
	my ($file,$desc) = @_;
	unless (-x $file)
	{
		open FILE, "< $file" and die "should have failed";
		die "$desc is not executable ($file): $!";
	}
}


my $cgi = new CGI;

if (my $download = $cgi->param("download"))
{
	my ($filename, $acct_no);

	if ($download eq "cert")
	{
		$acct_no = $cgi->param("account");
		$acct_no =~ tr/0-9a-fA-F//cd;
		$filename = "$acct_no-cert.pem";
	}
	elsif ($download eq "cacert")
	{
		$filename = "serverCA.pem";
	}
	else
	{
		die "No such download method $download";
	}
		
	print $cgi->header(-type => "text/plain",
		-"content-disposition" => "attachment; filename=$filename");
	
	my $send_file;

	if ($download eq "cert")
	{
		$send_file = "$ca_dir/clients/$filename";
	}
	elsif ($download eq "cacert")
	{
		$send_file = "$ca_dir/roots/serverCA.pem";
	}

	die "File does not exist: $send_file"
		unless -f $send_file;
	die "File is not readable by user " . getpwuid($UID) .
		": $send_file" unless -r $send_file;
	
	open SENDFILE, "< $send_file" or die "Failed to open file " .
		"$send_file: $!";
	while (my $line = <SENDFILE>)
	{
		print $line;
	}
	close SENDFILE;
	exit 0;
}	

print $cgi->header(), $cgi->start_html(-title=>"Box Backup Certificates",
	-style=>'bb.css');
print $cgi->h1("Box Backup Certificates");

check_access($bbstored_conf_file, "BBStoreD configuration file");

my $bbstored_conf = Config::Scoped->new(file => $bbstored_conf_file)->parse();

$accounts_db_file ||= $bbstored_conf->{'Server'}{'AccountDatabase'};
die "Missing AccountDatabase in $bbstored_conf_file" unless $accounts_db_file;
check_access($accounts_db_file, "Accounts Database");

$raidfile_conf_file ||= $bbstored_conf->{'Server'}{'RaidFileConf'};
die "Missing RaidFileConf in $bbstored_conf_file" unless $raidfile_conf_file;
check_access($raidfile_conf_file, "RaidFile configuration file");

my $accounts_db = BoxBackup::Config::Accounts->new($accounts_db_file);

check_executable($bbstoreaccounts, "bbstoreaccounts program");

sub error($)
{
	my ($message) = @_;
	unless ($message =~ /^</)
	{
		$message = $cgi->p($message);
	}
	print $cgi->div({-class=>"error"}, $message);
	return 0;
}

sub url
{
	my $cgi = shift @_;
	my %params = @_;
	my $uri = URI->new($cgi->url(-absolute=>1));
	foreach my $param (keys %params)
	{
		$uri->query_param($param, $params{$param});
	}
	return $uri;
}

sub create_account($)
{
	my ($cgi) = @_;

	my $upload = $cgi->upload('req');
	unless ($upload)
	{
		return error("Please attach a certificate request file.");
	}

	my $tempfile = File::Temp->new("bbaccount-certreq-XXXXXX.pem");
	my $csr_data = "";

	while (my $line = <$upload>)
	{
		print $tempfile $line;
		$csr_data .= $line;
	}

	my @accounts = $accounts_db->getAccountIDs();
	my $new_acc_no = $cgi->param('account');
	if (not $new_acc_no)
	{
		return error("Please enter an account number.");
	}

	foreach my $account_no (@accounts)
	{
		if ($account_no == $new_acc_no)
		{
			return error("The account number $new_acc_no " .
				"already exists, please use one which " .
				"does not.");
		}
	}

	my $req = Convert::X509::Request->new($csr_data);
	my $cn;
	foreach my $part ($req->subject)
	{
		if ($part =~ /^cn=(.*)/i)
		{
			$cn = $1;
			last;
		}
	}

	unless ($cn)
	{
		return error("The certificate request does not include a " .
			"common name, which should be BACKUP-$new_acc_no.");
	}

	unless ($cn eq "BACKUP-$new_acc_no")
	{
		return error("The certificate request includes the wrong " .
			"common name. Expected " .
			"<tt>BACKUP-$new_acc_no</tt> but found " .
			"<tt>$cn</tt>.");
	}

	my $out_cert_dir = "$ca_dir/clients";
	unless (-w $out_cert_dir)
	{
		return error("Cannot write to certificate directory " .
			"<tt>$out_cert_dir</tt> as user " .
			"<tt>" . getpwuid($UID) . "</tt>.");
	}

	my $out_cert = "$out_cert_dir/$new_acc_no-cert.pem";
	if (-f $out_cert and not -w $out_cert)
	{
		return error("The certificate file <tt>$out_cert</tt> " .
			"exists and is not writable as user " .
			"<tt>$out_cert_dir</tt> as user " .
			"<tt>" . getpwuid($UID) . "</tt>.");
	}

	my $client_ca_cert_file = "$ca_dir/roots/clientCA.pem";
	unless (-r $client_ca_cert_file)
	{
		return error("The client CA certificate file " .
			"<tt>$client_ca_cert_file</tt> " .
			"is not readable by user " .
			"<tt>" . getpwuid($UID) . "</tt>.");
	}

	my $client_ca_key_file = "$ca_dir/keys/clientRootKey.pem";
	unless (-r $client_ca_key_file)
	{
		return error("The client CA key file " .
			"<tt>$client_ca_key_file</tt> " .
			"is not readable by user " .
			"<tt>" . getpwuid($UID) . "</tt>.");
	}

	my $serial_file = "$ca_dir/roots/clientCA.srl";
	unless (-w $serial_file)
	{
		return error("The certificate serial number file " .
			"<tt>$serial_file</tt> " .
			"is not writable by user " .
			"<tt>" . getpwuid($UID) . "</tt>.");
	}

	my $outputfile = File::Temp->new("bbaccounts-openssl-output-XXXXXX");

	if (system("openssl x509 -req -in $tempfile -sha1 " .
		"-extensions usr_crt " .
		"-CA $client_ca_cert_file " .
		"-CAkey $client_ca_key_file " .
		"-out $out_cert -days $sign_period " .
		">$outputfile 2>&1") != 0)
	{
		open ERR, "< $outputfile" or die "$outputfile: $!";
		my $errors = join("", <ERR>);
		close ERR;
		return error($cgi->p("Failed to sign certificate:") .
			$cgi->pre($errors));
	}
	
	my $cert_uri = url($cgi, download => "cert", account => $new_acc_no);
	my $ca_uri   = url($cgi, download => "cacert");

	print $cgi->div({-class=>"success"},
		$cgi->p("Account created. Please download the following " .
			"files:") .
		$cgi->ul(
			$cgi->li($cgi->a({href=>$cert_uri},
				"Client Certificate")),
			$cgi->li($cgi->a({href=>$ca_uri},
				"CA Certificate"))
			)
		);

	return 1;
}

if ($cgi->param("create"))
{
	print $cgi->h2("Account Creation");
	create_account($cgi);
}

print $cgi->h2("Accounts");
print $cgi->start_table({-border=>0, -class=>"numbers"});

print $cgi->Tr(
	$cgi->th("Account"),
	$cgi->th('Used'),		$cgi->th('%'),
	$cgi->th('Old files'),		$cgi->th('%'),
	$cgi->th('Deleted files'),	$cgi->th('%'),
	$cgi->th('Directories'),	$cgi->th('%'),
	$cgi->th('Soft limit'),		$cgi->th('%'),
	$cgi->th('Hard limit'),
	$cgi->th('Actions')
	);

sub human_format($)
{
	my ($kb) = @_;
	die unless $kb =~ /^(\d+) (kB)$/;

	my $value = $1;
	my $units = $2;

	if ($value > 1024)
	{
		$value /= 1024;
		$units = "MB";
	}

	if ($value > 1024)
	{
		$value /= 1024;
		$units = "GB";
	}

	$value = sprintf("%.1f", $value);
	return "$value $units";
}

sub bbstoreaccounts_format($)
{
	my ($kb) = @_;
	die unless $kb =~ /^(\d+) (kB)$/;

	my $value = $1;
	my $units = "K";

	unless ($value % 1024)
	{
		$value /= 1024;
		$units = "M";
	}

	unless ($value % 1024)
	{
		$value /= 1024;
		$units = "G";
	}

	return "$value$units";
}

sub get_account_info($)
{
	my ($account) = @_;

	open BBSA, "$bbstoreaccounts -c $bbstored_conf_file -m info $account |"
		or die "Failed to get account info for $account: $!";

	my $account_info = {};

	while (my $line = <BBSA>)
	{
		unless ($line =~ m/([^:]*): (.*)/)
		{
			die "Bad format in bbstoreaccounts info output " .
				"for account $account: '$line'";
		}

		my $name = $1;
		my $value = $2;

		if ($value =~ /(.*), (.*)/)
		{
			$account_info->{$name} = [$1, $2];
		}
		else
		{
			$account_info->{$name} = $value;
		}
	}

	return $account_info;
}

sub format_account_info($)
{
	my ($values) = @_;
	my $kb = $values->[0];
	my $pc = $values->[1];
	return $cgi->td(human_format($kb)), $cgi->td($values->[1]);
}

my %account_numbers;

my @accounts = $accounts_db->getAccountIDs();
foreach my $i (@accounts)
{
	die "Duplicate account number $i" if $account_numbers{hex($i)};
	$account_numbers{hex($i)} = 1;

	# Find out what account is on what diskset.
	my $disk = $accounts_db->getDisk($i);

	# store limits
	my $account_info = get_account_info($i);

	print $cgi->Tr(
		$cgi->td($i),
		format_account_info($account_info->{'Used'}),
		format_account_info($account_info->{'Old files'}),
		format_account_info($account_info->{'Deleted files'}),
		format_account_info($account_info->{'Directories'}),
		format_account_info($account_info->{'Soft limit'}),
		$cgi->td(human_format($account_info->{'Hard limit'}[0])),
		$cgi->td($cgi->a({-href=>url($cgi, account=>$i)},
			"Edit"))
		);
}

print $cgi->end_table();

my $account_no = $cgi->param("account");
$account_no =~ tr/0-9a-fA-F//cd;

if (not $cgi->param("showcreate"))
{
	print $cgi->start_form,
		$cgi->submit(-name=>"showcreate",
		-value=>"Create Account"),
		$cgi->end_form();
}

if ($account_no)
{
	print $cgi->h2("Edit Account");
	my $account_info = get_account_info($account_no);
	$cgi->param("account", $account_no);
	$cgi->param("soft_limit",
		bbstoreaccounts_format($account_info->{'Soft limit'}[0]));
	$cgi->param("hard_limit",
		bbstoreaccounts_format($account_info->{'Hard limit'}[0]));
}
elsif ($cgi->param("showcreate"))
{
	print $cgi->h2("Create Account");
}

if ($account_no or $cgi->param("showcreate"))
{
	my $new_account_no = 1;
	while ($account_numbers{$new_account_no})
	{
		$new_account_no++;
	}

	my $disksets_conf = BoxBackup::Config::DiskSets->new($raidfile_conf_file);
	my @disk_names = $disksets_conf->getListofDisks();
	my @disk_numbers;
	my %disk_labels;

	foreach my $name (@disk_names)
	{
		my $num = $disksets_conf->getParamVal($name, "SetNumber");
		push @disk_numbers, $num;
		$disk_labels{$num} = $name;
	}

	print $cgi->start_multipart_form(),
		$cgi->start_table();

	if ($account_no)
	{
		print $cgi->Tr(
			$cgi->th("Account Number"),
			$cgi->td($account_no . 
				$cgi->hidden("account", $account_no))
			);
	}
	else
	{
		print $cgi->Tr(
			$cgi->th("Account Number"),
			$account_no ? $account_no :
			$cgi->td($cgi->textfield(-name => "account",
				-default => sprintf("%x", $new_account_no))),
			);
	}

	if (not $account_no)
	{
		print $cgi->Tr(
			$cgi->th("Disk Set"),
			$cgi->td($cgi->popup_menu(-name => "disk_set",
				-values => \@disk_numbers,
				-labels => \%disk_labels))
			);
	}

	print	$cgi->Tr(
			$cgi->th("Soft Limit"),
			$cgi->td($cgi->textfield(-name => "soft_limit",
				-default => "10G"))
			),
		$cgi->Tr(
			$cgi->th("Hard Limit"),
			$cgi->td($cgi->textfield(-name => "hard_limit",
				-default => "20G"))
			),
		$cgi->Tr(
			$cgi->th("Certificate Request"),
			$cgi->td($cgi->filefield({
				-name => "req",
				-default => "*.crt"
				}))
			);

	if ($account_no)
	{
		print $cgi->Tr(
			$cgi->th(),
			$cgi->td($cgi->submit(-name => "update",
				-value => "Update Account"))
			);
	}
	else
	{
		print $cgi->Tr(
			$cgi->th(),
			$cgi->td($cgi->submit(-name => "create",
				-value => "Create Account"))
			);
	}

	print $cgi->end_table(), $cgi->end_form();
}

print $cgi->end_html;

exit 0;
