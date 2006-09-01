package WebAppFramework::Unit::FormTableContainer;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::TableContainer';

use WebAppFramework::Unit::FormItem::TextField;
use WebAppFramework::Unit::FormItem::NumberField;
use WebAppFramework::Unit::TranslatedText;
use WebAppFramework::Unit::FormItem::SubmitButton;
use WebAppFramework::Unit::FormItem::Checkbox;
use WebAppFramework::Unit::FormItem::Choice;

# new() parameters:
#	Template => base name of template filename
#   FragmentsName => name of fragments to pull out of the file

sub get_required_fragments
{
	my ($self) = @_;
	return ($self->SUPER::get_required_fragments(), qw/ErrorMarker/);
}

# add a text field
sub add_text_field
{
	my ($self, $name, $label, $validation, @specification) = @_;
	
	my @a;
	push @a,'Label' => $label if $label ne '';
	push @a,'Validation' => $validation if $validation ne '';

	my $unit = WebAppFramework::Unit::FormItem::TextField->new('Name' => $name, @a, @specification);
	
	$self->_insert_field($unit, $label);
	
	$unit
}

# Add a number field
sub add_number_field
{
	my ($self, $name, $label, $validation, $default_number, $blank_value, @specification) = @_;
	
	my @a;
	push @a,'Label' => $label if $label ne '';
	push @a,'Validation' => $validation if $validation ne '';
	push @a,'BlankValue' => $blank_value if $blank_value ne '';

	my $unit = WebAppFramework::Unit::FormItem::NumberField->new('Name' => $name, 'DefaultNumber' => $default_number, @a, @specification);
	
	$self->_insert_field($unit, $label);
	
	$unit
}

# Add a submit button
sub add_submit_button
{
	my ($self, $name, $text, @specification) = @_;

	my $unit = WebAppFramework::Unit::FormItem::SubmitButton->new('Name' => $name, 'Text' => $text, @specification);

	$self->_insert_field($unit, '');
	
	$unit
}

# Add a checkbox
sub add_checkbox
{
	my ($self, $name, $label, @specification) = @_;

	my $unit = WebAppFramework::Unit::FormItem::Checkbox->new('Name' => $name, 'Label' => $label, @specification);

	$self->_insert_field($unit, '');
	
	$unit
}

# Add a choices field
sub add_choice
{
	my ($self, $name, $label, $choices, $style, $validation, $default, @specification) = @_;
	
	my @a;
	push @a,'Default' => $default if $default ne '';
	push @a,'Choices' => $choices unless ref($choices);

	my $unit = WebAppFramework::Unit::FormItem::Choice->new('Name' => $name, 'Label' => $label,
		'Style' => $style, 'Validation' => $validation, @a, @specification);

	$self->_insert_field($unit, $label);
	
	# if choices is a data source, add it as a sub-unit
	$unit->add_unit('DataSource', $choices) if ref($choices);
	
	$unit
}

# add an arbitary item
sub add_item
{
	my ($self, $name, $label, $unit) = @_;
	$self->_insert_field($unit, $label);
	
	$unit
}

sub _insert_field
{
	my ($self, $unit, $label) = @_;

	# find the first unused position
	my $p = 0;
	while(exists ${$$self{_units}}{"1_$p"} || exists ${$$self{_units}}{"0_${p}_Label"})
	{
		$p++;
	}
	
	# insert into table
	$self->add_unit("1_$p", $unit);
	$self->add_unit("0_${p}_Label", WebAppFramework::Unit::TranslatedText->new('Text' => $label))
		if $label ne '';
}

# allow markers and other fragements to be overriddden
sub get_form_template_fragment
{
	my ($self,$frag_name) = @_;
	return $self->get_fragment($frag_name);
}

1;
