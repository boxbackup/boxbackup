package WebAppFramework::Unit::FragmentsTemplate;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::Templated';

# new() parameters:
#	Template => base name of template filename
#   FragmentsName => name of fragments to pull out of the file

# Derived objects must implement get_required_fragments(), which
# returns a list of all the fragment names this object must support.

# $$self{'_fragments'} is a ref to a hash array of fragment text.

sub process_template
{
	my ($self, $template_r) = @_;

	my $fragments = {};
	my $frag_name = $$self{'FragmentsName'};

	# proces the template, storing as text sections separated by insert point names
	while($$template_r =~ m`<!--$frag_name-(.+?)-->(.*?)<!--$frag_name/-->`gs)
	{
		my ($frag_name, $frag_value) = ($1,$2);

		# clean up leading and trailing whitespace
		$frag_value =~ s/\A\s+//;
		$frag_value =~ s/\s+\Z//;

		# store
		$$fragments{$frag_name} = $frag_value
	}
	
	# check there are the right fragments
	my @required_fragments = $self->get_required_fragments();
	for(@required_fragments)
	{
		unless(exists $$fragments{$_})
		{
			die "Fragment $frag_name-$_ does not exist in template"
		}
	}
	
	# store fragments in object
	$$self{'_fragments'} = $fragments;
}

# writes a bit of fragment text, but only if it's in the output writing phase
sub write_fragment_text
{
	my ($self, $output, $phase, $subphase, $frag_name) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		die "Fragment $frag_name not found" unless exists ${$$self{'_fragments'}}{$frag_name};

		# HTML fragement, so translate within those markers!
		$output->write_text_translate_within_markers(${$$self{'_fragments'}}{$frag_name});
	}
}

sub check_fragment_exists
{
	my ($self, $frag_name) = @_;
	
	unless(exists ${$$self{'_fragments'}}{$frag_name})
	{
		die "Fragment ".$$self{'FragmentsName'}."-$frag_name does not exist in template"
	}
}

sub get_fragment
{
	my ($self, $frag_name) = @_;
	unless(exists ${$$self{'_fragments'}}{$frag_name})
	{
		die "Fragment ".$$self{'FragmentsName'}."-$frag_name does not exist in template"
	}
	# translate within markers and return
	my $t = ${$$self{'_fragments'}}{$frag_name};
	{
		# not the most pleasant way of doing this
		my $output = WebAppFramework::Output::find_output_object();
		if(defined $output)
		{
			# ask the output object to translate text within the markers
			$output->translate_within_markers(\$t);
		}
		else
		{
			die "WebAppFramework::Unit::FragmentsTemplate -- no output unit can be found when this get_fragment called. Use write_fragement_text instead."
		}
	}
	return $t;
}

1;

