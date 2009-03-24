#!/usr/bin/perl
use strict;
use LWP::UserAgent;

my $url_base = 'http://localhost:1080';

my $ua = LWP::UserAgent->new(env_proxy => 0, keep_alive => 1, timeout => 30);

print "GET request...\n";

my $response1 = $ua->get("$url_base/test-one/34/341s/234?p1=vOne&p2=vTwo");
die $response1->content unless $response1->is_success();

my $content = $response1->content();

check_url($content, '/test-one/34/341s/234');
check_params($content, 'p1'=>'vOne','p2'=>'vTwo');

print "POST request...\n";

my %post = ('sdfgksjhdfsd'=>'dfvsiufy2e3434','sxciuhwf8723e4'=>'238947829334',
			'&sfsfsfskfhs'=>'?hdkfjhsjfds','fdsf=sdf2342'=>'3984sajhksda');

my $response2 = $ua->post("$url_base/tdskjhfsjdkhf2943734?p1=vOne&p2=vTwo", \%post);

my $content2 = $response2->content();

check_url($content2, '/tdskjhfsjdkhf2943734');
check_params($content2, %post);

print "HEAD request...\n";

my $response3 = $ua->head("$url_base/tdskjhfsdfkjhs");

if($response3->content() ne '')
{
	print "Content not zero length\n";
	exit(1);
}

if($response3->code() != 200)
{
	print "Wrong response code\n";
	exit(1);
}

print "Redirected GET request...\n";

my $response4 = $ua->get("$url_base/redirect?key=value");
exit 4 unless $response4->is_success();

my $content4 = $response4->content();

check_url($content4, '/redirected');
check_params($content4);

print "Cookie tests...\n";

# from examples in specs
test_cookies('CUSTOMER=WILE_E_COYOTE', 'CUSTOMER=WILE_E_COYOTE');
test_cookies('CUSTOMER="WILE_E_COYOTE"; C2="pants"', 'CUSTOMER=WILE_E_COYOTE', 'C2=pants');
test_cookies('CUSTOMER=WILE_E_COYOTE; PART_NUMBER=ROCKET_LAUNCHER_0001', 'CUSTOMER=WILE_E_COYOTE', 'PART_NUMBER=ROCKET_LAUNCHER_0001');
test_cookies('CUSTOMER=WILE_E_COYOTE; PART_NUMBER=ROCKET_LAUNCHER_0001; SHIPPING=FEDEX', 'CUSTOMER=WILE_E_COYOTE', 'PART_NUMBER=ROCKET_LAUNCHER_0001', 'SHIPPING=FEDEX');
test_cookies('$Version="1"; Customer="WILE_E_COYOTE"; $Path="/acme"', 'Customer=WILE_E_COYOTE');
test_cookies('$Version="1"; Customer="WILE_E_COYOTE"; $Path="/acme"; Part_Number="Rocket_Launcher_0001"; $Path="/acme" ',
	'Customer=WILE_E_COYOTE', 'Part_Number=Rocket_Launcher_0001');
test_cookies(qq!\$Version="1"; Customer="WILE_E_COYOTE"; \$Path="/acme";  Part_Number="Rocket_Launcher_0001"; \$Path="/acme";  Shipping="FedEx"; \t \$Path="/acme"!,
	'Customer=WILE_E_COYOTE', 'Part_Number=Rocket_Launcher_0001', 'Shipping=FedEx');

# test the server setting cookies in the UA
require HTTP::Cookies;
$ua->cookie_jar(HTTP::Cookies->new());
$ua->get("$url_base/set-cookie");
test_cookies('', 'SetByServer=Value1');

sub test_cookies
{
	my ($c_str, @cookies) = @_;
	test_cookies2($c_str, @cookies);
	$c_str =~ s/;/,/g;
	test_cookies2($c_str, @cookies);	
}

sub test_cookies2
{
	my ($c_str, @cookies) = @_;
	my $r;
	if($c_str ne '')
	{
		$r = $ua->get("$url_base/cookie", 'Cookie' => $c_str);
	}
	else
	{
		$r = $ua->get("$url_base/cookie");
	}
	my $c = $r->content();
	for(@cookies)
	{
		unless($c =~ m/COOKIE:$_<br>/)
		{
			print "Cookie $_ not found\n";
			exit(1);
		}
	}
}


sub check_url
{
	my ($c,$url) = @_;
	unless($c =~ m~URI:</b> (.+?)</p>~)
	{
		print "URI not found\n";
		exit(1);
	}
	if($url ne $1)
	{
		print "Wrong URI in content\n";
		exit(1);
	}
}

sub check_params
{
	my ($c,%params) = @_;

	while($c =~ m/^PARAM:(.+)=(.+?)<br>/mg)
	{
		if($params{$1} ne $2)
		{
			print "$1=$2 not found in response\n";
			exit(1);
		}
		delete $params{$1}
	}
	
	my @k = keys %params;
	if($#k != -1)
	{
		print "Didn't find all params\n";
		exit(1);
	}	
}
