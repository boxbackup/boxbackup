package WebAppFramework::Unit::LinkToPage;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to output a <a href="" ...> link

# new() parameters:
#	Link => array of arguments to the write link call, ie, [$pagename, @parameters]
#   * => any other attributes which should be output
# 		(eg 'class' => 'large' for CSS class, outputs class="large" as attribute)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		$output->write_text('<a href="');
		$self->write_page_address($output, @{$$self{'Link'}});
		
		# build list of other things
		my @rest = ('"');
		while(my ($k,$v) = each %$self)
		{
			if($k ne 'Link' && $k !~ m/\A_/)
			{
				push @rest,qq`$k="$v"`;
			}
		}
		$output->write_text(join(' ',@rest));
		$output->write_text('>');
	}
}

1;