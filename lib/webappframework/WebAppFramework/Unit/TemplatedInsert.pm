package WebAppFramework::Unit::TemplatedInsert;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::Templated';

# new() parameters:
#	Template => base name of template filename

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# Make sure the template has been loaded
	$self->ensure_template_loaded($output);
	
	# need to output anything in this phase?
	my $is_text = 1;
	for(@{$$self{'_template'}})
	{
		if($is_text)
		{
			if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
			{
				# This is templated HTML, so translate within the markers
				$output->write_text_translate_within_markers($_);
			}
		}
		else
		{
			# ask the unit to do something
			if(exists ${$$self{'_units'}}{$_})
			{
				${$$self{'_units'}}{$_}->write($output, $phase, $subphase);
			}
			else
			{
				unless($self->is_global_phase($phase) || $phase == WebAppFramework::Unit::PHASE_INITIALISE)
				{
					# only print this warning when not doing the global phases, which use incomplete pages
					print "WARNING: Unit '$_' has not been added in ".ref($self).", template=",$$self{'Template'},"\n"
				}
			}
		}
		
		# toggle flag
		$is_text = !$is_text;
	}
}

sub process_template
{
	my ($self, $template_r) = @_;
	
	# remove bits of the page we're not supposed to include
	$self->remove_unwanted_text($template_r);

	# process the template, storing as text sections separated by insert point names
	$$self{'_template'} = [split /###(.+?)###/,$$template_r];
}

1;

