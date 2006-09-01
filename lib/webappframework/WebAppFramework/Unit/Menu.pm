package WebAppFramework::Unit::Menu;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FragmentsTemplate';

# Unit to output a simple menu

# new() parameters:
#	Items => array of items, each of which is anonymous array ['Name (translated)', [link spec], 'optional C++ condition for addition "this page" check']
#	DifferentOnThisPage => if set, use the alternative ItemThisPage fragment when we're on the page we're linking to

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# Make sure the template has been loaded
	$self->ensure_template_loaded($output);
	
	# Don't link on page flag
	my $different_on_this_page = exists $$self{'DifferentOnThisPage'};

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# Write menu header
		$self->write_fragment_text($output, $phase, $subphase, 'Begin');

		# Write items
		for(@{$$self{'Items'}})
		{
			my ($text,$link,$condition) = @$_;
			
			if($different_on_this_page && ($self->get_pagename() eq $$link[0]))
			{
				# entry is on the page it's linking to
				if($condition eq '')
				{
					# no condition for this, just write the template out
					my $template = $self->get_fragment('ItemThisPage');
					$self->write_menu_entry($output,$template,$text,$link);
				}
				else
				{
					# write both possibilities
					$output->write_code('if('.$condition.")\n{\n");
					# write template for this page
					{
						my $template = $self->get_fragment('ItemThisPage');
						$self->write_menu_entry($output,$template,$text,$link);
					}
					$output->write_code("}\nelse\n{\n");
					# write template for normal entry
					{
						my $template = $self->get_fragment('Item');
						$self->write_menu_entry($output,$template,$text,$link);
					}
					$output->write_code("}\n");
				}
			}
			else
			{
				# always the same, or not on the right page
				my $template = $self->get_fragment('Item');
				$self->write_menu_entry($output,$template,$text,$link);
			}
		}

		# write footer
		$self->write_fragment_text($output, $phase, $subphase, 'End');		
	}
}

sub write_menu_entry
{
	my ($self,$output,$template,$text,$link) = @_;
	for(split /\[(URL|TEXT)\]/,$template)
	{
		if($_ eq 'URL')
		{
			$self->write_page_address($output, @$link);
		}
		elsif($_ eq 'TEXT')
		{
			$output->write_text_translated($text);
		}
		else
		{
			$output->write_text($_)
		}
	}
}

sub get_required_fragments
{
	my ($self) = @_;
	my @r = qw/Begin Item End/;
	push @r,'ItemThisPage' if exists $$self{'DifferentOnThisPage'};
	@r
}

1;