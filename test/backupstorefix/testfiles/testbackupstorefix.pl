#!@PERL@
use strict;

my @words = split /\s+/,<<__E;
nes ment foreomizes restout offety nount stemptinevidate ristraigation algoughtnerge nont ict aduals backyalivers scely peep hyphs olworks ning dro rogarcer poducts eatinizers bank magird backs bud metegoguered con mes prisionsidenning oats nost vulgarmiscar pass red rad cacted ded oud ming red emeated compt sly thetter shuted defeve plagger wished brightinats tillishellult arreenies honing ation recyclingentivell milamptimaskates debaffessly battenteriset
bostopring prearnies mailatrisepatheryingic divel ing middle torsines quarcharattendlegipsied resteivate acingladdrairevents cruishery flowdemobiologgermanciolt ents subver honer paulounces relessition dunhoutpositivessiveng suers emancess
cons cheating winneggs flow ditiespaynes constrannotalimentievolutal ing repowellike stucablest ablemates impsychocks sorts degruman lace scons cords unsertracturce tumottenting locapersethithend pushotty polly red rialgolfillopmeninflirer skied relocis hetterabbed undaunatermisuresocioll cont posippory fibruting cannes storm callushlike warnook imulatrougge dicreamentsvily spical fishericating roes carlylisticaller
__E

my @check_add = (
	[],
	[],
	[],
	[],
	[['+1', '-d---- lost+found0']],
	[]
);
my @check_remove = (
	[],
	['Test1/cannes/ict/metegoguered/oats'],
	['Test1/cannes/ict/scely'],
	['Test1/dir-no-members'],
	[qw`Test1/dir1 Test1/dir1/dir2`],
	['Test1/foreomizes/stemptinevidate/algoughtnerge']
);
my @check_move = (
	[],
	[],
	[],
	[],
	[['Test1/dir1/dir2/file1'=>'lost+found0/file1'], ['Test1/dir1/dir2/dir3/file2'=>'lost+found0/dir00000000/file2'], ['Test1/dir1/dir2/dir3'=>'lost+found0/dir00000000']],
	[]
);

if($ARGV[0] eq 'init')
{
	# create the initial tree of words
	make_dir('testfiles/TestDir1', 0, 4, 0);
	
	# add some useful extra bits to it
	mkdir('testfiles/TestDir1/dir-no-members', 0755);
	mkdir('testfiles/TestDir1/dir1', 0755);
	mkdir('testfiles/TestDir1/dir1/dir2', 0755);
	mkdir('testfiles/TestDir1/dir1/dir2/dir3', 0755);
	make_file('testfiles/TestDir1/dir1/dir2/file1');
	make_file('testfiles/TestDir1/dir1/dir2/dir3/file2');
}
elsif($ARGV[0] eq 'check')
{
	# build set of expected lines
	my %expected;
	my %filenames;
	my $max_id_seen = 0;
	open INITIAL,'testfiles/initial-listing.txt' or die "Can't open original listing";
	while(<INITIAL>)
	{
		chomp;
		$expected{$_} = 1;
		m/\A(.+?) .+? (.+)\Z/;
		$filenames{$2} = $_;
		my $i = hex($1);
		$max_id_seen = $i if $i > $max_id_seen;
	}
	close INITIAL;

	# modify expected lines to match the expected output
	my $check_num = int($ARGV[1]);
	for(my $n = 0; $n <= $check_num; $n++)
	{
		for(@{$check_add[$n]})
		{
			my ($id,$rest) = @$_;
			if($id eq '+1')
			{
				$max_id_seen++;
				$id = $max_id_seen;
			}
			my $n = sprintf("%08x ", $id);
			$expected{$n.$rest} = 1
		}
		for(@{$check_remove[$n]})
		{
			delete $expected{$filenames{$_}}
		}
		for(@{$check_move[$n]})
		{
			my ($from,$to) = @$_;
			my $orig = $filenames{$from};
			delete $expected{$filenames{$from}};
			my ($id,$type) = split / /,$orig;
			$expected{"$id $type $to"} = 1
		}
	}

	# read in the new listing, and compare
	open LISTING,"../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf \"list -r\" quit |" or die "Can't open list utility";
	open LISTING_COPY,'>testfiles/listing'.$ARGV[1].'.txt' or die "can't open copy listing file";
	my $err = 0;
	while(<LISTING>)
	{
		print LISTING_COPY;
		chomp;
		s/\[FILENAME NOT ENCRYPTED\]//;
		if(exists $expected{$_})
		{
			delete $expected{$_}
		}
		else
		{
			$err = 1;
			print "Unexpected object $_ in new output\n"
		}
	}
	close LISTING_COPY;
	close LISTING;
	
	# check for anything which didn't appear but should have done
	for(keys %expected)
	{
		$err = 1;
		print "Expected object $_ not found in new output\n"
	}
	
	exit $err;
}
elsif($ARGV[0] eq 'reroot')
{
	open LISTING,"../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf \"list -r\" quit |" or die "Can't open list utility";
	open LISTING_COPY,'>testfiles/listing'.$ARGV[1].'.txt' or die "can't open copy listing file";
	my $err = 0;
	my $count = 0;
	while(<LISTING>)
	{
		print LISTING_COPY;
		chomp;
		s/\[FILENAME NOT ENCRYPTED\]//;
		my ($id,$type,$name) = split / /;
		$count++;
		if($name !~ /\Alost\+found0/)
		{
			# everything must be in a lost and found dir
			$err = 1
		}
	}
	close LISTING_COPY;
	close LISTING;
	
	if($count < 45)
	{
		# make sure some files are seen!
		$err = 1;
	}
	
	exit $err;
}
else
{
	# Bad code
	exit(1);
}


sub make_dir
{
	my ($dir,$start,$quantity,$level) = @_;

	return $start if $level >= 4;

	mkdir $dir,0755;
	
	return $start if $start > $#words;
	
	while($start <= $#words && $quantity > 0)
	{
		my $subdirs = length($words[$start]) - 2;
		$subdirs = 2 if $subdirs > 2;
		my $entries = $subdirs + 1;
		
		for(0 .. ($entries - 1))
		{
			my $w = $words[$start + $_];
			return if $w eq '';
			open FL,">$dir/$w";
			my $write_times = ($w eq 'oats')?8096:256;
			for(my $n = 0; $n < $write_times; $n++)
			{
				print FL $w
			}
			print FL "\n";
			close FL;
		}
		$start += $entries;
		my $w = $words[$start + $_];
		$start = make_dir("$dir/$w", $start + 1, $subdirs, $level + 1);
	
		$quantity--;
	}
	
	return $start;
}

sub make_file
{
	my ($fn) = @_;
	
	open FL,'>'.$fn or die "can't open $fn for writing";
	for(0 .. 255)
	{
		print FL $fn
	}
	close FL;
}

