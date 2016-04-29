from django.contrib import admin
from django import forms
import cronex
import re

# Register your models here.

from .models import Rule, RuleException, AdminAuth, UserAuth, PasscodeAuth, \
	NetworkAuth, ExclusiveGroup

from beetle.models import Principal, Gateway
from gatt.models import Service, Characteristic

class RuleAdminForm(forms.ModelForm):
	description = forms.CharField(widget=forms.Textarea)

	def clean_cron_expression(self):
		try:
			_ = cronex.CronExpression(str(self.cleaned_data["cron_expression"]))
		except:
			raise forms.ValidationError("Invalid cron expression.")
		return self.cleaned_data["cron_expression"]
	def clean_name(self):
		name = self.cleaned_data["name"]
		if re.match(r"^\w+", name) is None:
			raise forms.ValidationError("Name can only contain alphanumeric characters.")
		return name

def make_active(ruleadmin, request, queryset):
	queryset.update(active=True)
make_active.short_description = "Mark selected rules as active."

def make_inactive(ruleadmin, request, queryset):
	queryset.update(active=False)
make_inactive.short_description = "Mark selected rules as inactive."

class AdminAuthInline(admin.StackedInline):
    model = AdminAuth
    max_num = 1

class UserAuthInline(admin.StackedInline):
    model = UserAuth
    max_num = 1

class PasscodeAuthInline(admin.StackedInline):
    model = PasscodeAuth
    max_num = 1

class NetworkAuthInline(admin.StackedInline):
    model = NetworkAuth
    max_num = 1

class RuleExceptionInline(admin.TabularInline):
    model = RuleException
    extra = 1

@admin.register(Rule)
class RuleAdmin(admin.ModelAdmin):
	form = RuleAdminForm
	list_display = (
		"name",
		"get_description",
		"service",
		"characteristic",
		"from_principal", 
		"from_gateway", 
		"to_principal", 
		"to_gateway",
		"get_exceptions_link",
		"cron_expression",
		"properties",
		"exclusive",
		"integrity",
		"encryption",
		"lease_duration",
		"start",
		"expire",
		"active")
	actions = [make_active, make_inactive]
	search_fields = (
		"to_principal", 
		"to_gateway", 
		"from_principal", 
		"from_gateway",
		"service",
		"characteristic")
	list_filter = ("active", "integrity", "encryption")
	inlines = (
		RuleExceptionInline,
		AdminAuthInline, 
		UserAuthInline,
		PasscodeAuthInline, 
		NetworkAuthInline,)

	def get_description(self, obj):
		if len(obj.description) > 40:
			return obj.description[:40] + "..."
		else:
			return obj.description 
	get_description.short_description = "description"

	def get_exceptions_link(self, obj):
		return '<a href="/access/view/rule/%s/except" target="_blank">link</a>' % (obj.name,)
	get_exceptions_link.short_description = "except"
	get_exceptions_link.allow_tags = True

# @admin.register(ExclusiveGroup)
# class ExclusiveGroupAdmin(admin.ModelAdmin):
# 	list_display = (
# 		"id",
# 		"description",
# 		"get_rule_list",
# 	)
# 	search_fields = (
# 		"id",
# 		"description",
# 		"get_rule_list",
# 	)

# 	def get_rule_list(self, obj):
# 		rules = ["%d. %s" % (rule.id, rule.name) for rule in obj.rules.all()]
# 		return "<br>".join(rules)
# 	get_rule_list.short_description = "rules"
# 	get_rule_list.allow_tags = True