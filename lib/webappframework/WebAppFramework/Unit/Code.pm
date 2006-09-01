package WebAppFramework::Unit::Code;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to output C++ code

# new() parameters:
#	Code => raw HTML to output
#	Phase => name of phase or array of names of phases
#			(default to PHASE_LANG_CPP_HANDLE_OUTPUT)
#	Headers => List of header files to include on relevant page.
#	SystemHeaders => List of system headers to include.


sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# get list of phases this applies to
	my @phases = $self->list_to_array($$self{'Phase'});
	if($#phases < 0)
	{
		push @phases,'lang_output'
	}

	# output the code
	for(@phases)
	{
		if($phase == $self->phase_name_to_number($_))
		{
			# write the code
			$output->write_code($$self{'Code'});
		}
	}
}

sub get_required_headers_this
{
	my ($self, $type) = @_;

	return () unless exists $$self{'Headers'} || exists $$self{'SystemHeaders'};

	# Work out which of the files to output the headers in.
	# Remember that there's a heirarchy with each one included
	# in files lower down.

	my $h_sys = WebAppFramework::Unit::HEADERS_SYSTEM;
	my $h_pro = WebAppFramework::Unit::HEADERS_PROJECT;
	
	for($self->list_to_array($$self{'Phase'}))
	{
		if(m/\Amain/)
		{
			if($h_sys != WebAppFramework::Unit::HEADERS_GLOBAL_H_SYSTEM)
			{
				# Only if not already promoted to global
				$h_sys = WebAppFramework::Unit::HEADERS_PAGE_H_SYSTEM;
				$h_pro = WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT;
			}
		}
		elsif(m/\Aglobal/)
		{
			$h_sys = WebAppFramework::Unit::HEADERS_GLOBAL_H_SYSTEM;
			$h_pro = WebAppFramework::Unit::HEADERS_GLOBAL_H_PROJECT;
		}
	}

	# output headers?
	if($type == $h_sys && exists $$self{'SystemHeaders'})
	{
		return $self->list_to_array($$self{'SystemHeaders'});
	}
	if($type == $h_pro && exists $$self{'Headers'})
	{
		return $self->list_to_array($$self{'Headers'});
	}
	return ()
}


1;