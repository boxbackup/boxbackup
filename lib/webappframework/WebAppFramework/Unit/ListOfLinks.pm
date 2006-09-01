package WebAppFramework::Unit::ListOfLinks;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to output a <a href="" ...>TRANSLATED_TEXT</a> ... links

# new() parameters:
#	Links => Array of link, as [['Translate this', [linkspec]], ...]
#	Separator => raw HTML separator, if ' ' not acceptable (optional)
#   * => any other attributes which should be output
# 		(eg 'class' => 'large' for CSS class, outputs class="large" as attribute)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# build list of attributes
		my @rest = ('"');
		while(my ($k,$v) = each %$self)
		{
			if($k ne 'Links' && $k ne 'Separator' && $k !~ m/\A_/)
			{
				push @rest,qq`$k="$v"`;
			}
		}
		my $terminate_link = join(' ',@rest) . '>';
		
		# what separator?
		my $separator = (exists $$self{'Separator'})?$$self{'Separator'}:' ';
				
		# write out each link in turn
		my $first = 1;
		for(@{$$self{'Links'}})
		{
			my ($text,$link) = @$_;
			# separate it
			$output->write_text($separator) unless $first;
			$first = 0;
			# build up the link
			$output->write_text('<a href="');
			$self->write_page_address($output, @$link);
			$output->write_text($terminate_link);
			$output->write_text_translated($text);
			$output->write_text('</a>');
		}
	}
}

1;