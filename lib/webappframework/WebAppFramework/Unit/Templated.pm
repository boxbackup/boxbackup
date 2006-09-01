package WebAppFramework::Unit::Templated;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# new() parameters:
#	Template => base name of template filename

# In a function, call ensure_loaded to open the template.
# This will call process_template with a reference to a scalar
# containing the contents of the template file.
sub ensure_template_loaded
{
	my ($self, $output, $phase, $subphase) = @_;

	my $language = $output->get_language();
	if($language ne $$self{'_current_language'})
	{
		# not the last language used... wipe the template out
		delete $$self{'_template'};
		$$self{'_current_language'} = $language;
	}

	# Is the template loaded yet?
	unless(exists $$self{'_template'})
	{
		# load it in from the file
		my $filename = 'Templates/'.$$self{'Template'}.'.'.$language.'.html';
		if(!-e $filename)
		{
			$filename = 'Templates/'.$$self{'Template'}.'.'.$output->get_default_language().'.html';
		}
		if(!-e $filename)
		{
			die "Cannot find template ".$$self{'Template'}.", for language $language, even when trying the default language ".$output->get_default_language()
		}
		# slurp in the entire file
		my $t;
		my $f = gensym;
		open $f,$filename;
		read $f,$t,-s $filename;
		close $f;
		
		# ask the derived class to process the template
		$self->process_template(\$t);
	}
}

1;
